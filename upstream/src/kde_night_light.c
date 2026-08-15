#include "../include/kde_night_light.h"
#include "../include/log.h"

#ifdef GSR_DBUS

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <dbus/dbus.h>

#define NIGHT_LIGHT_POLL_SECONDS 1
#define NIGHT_LIGHT_TEMPERATURE_NEUTRAL 6500
#define NIGHT_LIGHT_TEMPERATURE_MIN 1000

typedef struct {
    double v[3];
} vec3d;

typedef struct {
    double m[9];
} mat3d;

struct gsr_kde_night_light {
    pthread_t thread;
    bool thread_created;
    atomic_bool stop;
    atomic_int temperature;
    int cached_temperature[2];
    gsr_color_chromaticities cached_container_primaries[2];
    float cached_inverse_matrix[2][9];
};

/* Blackbody whitepoint table for 1000K-6500K at 100K intervals, from https://github.com/jonls/redshift/blob/master/README-colorramp */
static const double blackbody_whitepoints[56][3] = {
    {1.00000000, 0.18172716, 0.00000000}, {1.00000000, 0.25503671, 0.00000000},
    {1.00000000, 0.30942099, 0.00000000}, {1.00000000, 0.35357379, 0.00000000},
    {1.00000000, 0.39091524, 0.00000000}, {1.00000000, 0.42322816, 0.00000000},
    {1.00000000, 0.45159884, 0.00000000}, {1.00000000, 0.47675916, 0.00000000},
    {1.00000000, 0.49923747, 0.00000000}, {1.00000000, 0.51943421, 0.00000000},
    {1.00000000, 0.54360078, 0.08679949}, {1.00000000, 0.56618736, 0.14065513},
    {1.00000000, 0.58734976, 0.18362641}, {1.00000000, 0.60724493, 0.22137978},
    {1.00000000, 0.62600248, 0.25591950}, {1.00000000, 0.64373109, 0.28819679},
    {1.00000000, 0.66052319, 0.31873863}, {1.00000000, 0.67645822, 0.34786758},
    {1.00000000, 0.69160518, 0.37579588}, {1.00000000, 0.70602449, 0.40267128},
    {1.00000000, 0.71976951, 0.42860152}, {1.00000000, 0.73288760, 0.45366838},
    {1.00000000, 0.74542112, 0.47793608}, {1.00000000, 0.75740814, 0.50145662},
    {1.00000000, 0.76888303, 0.52427322}, {1.00000000, 0.77987699, 0.54642268},
    {1.00000000, 0.79041843, 0.56793692}, {1.00000000, 0.80053332, 0.58884417},
    {1.00000000, 0.81024551, 0.60916971}, {1.00000000, 0.81957693, 0.62893653},
    {1.00000000, 0.82854786, 0.64816570}, {1.00000000, 0.83717703, 0.66687674},
    {1.00000000, 0.84548188, 0.68508786}, {1.00000000, 0.85347859, 0.70281616},
    {1.00000000, 0.86118227, 0.72007777}, {1.00000000, 0.86860704, 0.73688797},
    {1.00000000, 0.87576611, 0.75326132}, {1.00000000, 0.88267187, 0.76921169},
    {1.00000000, 0.88933596, 0.78475236}, {1.00000000, 0.89576933, 0.79989606},
    {1.00000000, 0.90198230, 0.81465502}, {1.00000000, 0.90963069, 0.82838210},
    {1.00000000, 0.91710889, 0.84190889}, {1.00000000, 0.92441842, 0.85523742},
    {1.00000000, 0.93156127, 0.86836903}, {1.00000000, 0.93853986, 0.88130458},
    {1.00000000, 0.94535695, 0.89404470}, {1.00000000, 0.95201559, 0.90658983},
    {1.00000000, 0.95851906, 0.91894041}, {1.00000000, 0.96487079, 0.93109690},
    {1.00000000, 0.97107439, 0.94305985}, {1.00000000, 0.97713351, 0.95482993},
    {1.00000000, 0.98305189, 0.96640795}, {1.00000000, 0.98883326, 0.97779486},
    {1.00000000, 0.99448139, 0.98899179}, {1.00000000, 1.00000000, 1.00000000}
};

/* The same channel factors that kde plasma uses for night light (kwin colortemperature.h, sampleColorTemperature) */
static vec3d night_light_channel_factors(int temperature) {
    if(temperature < NIGHT_LIGHT_TEMPERATURE_MIN)
        temperature = NIGHT_LIGHT_TEMPERATURE_MIN;
    if(temperature >= NIGHT_LIGHT_TEMPERATURE_NEUTRAL)
        return (vec3d){ .v = { 1.0, 1.0, 1.0 } };

    const int index = (temperature - NIGHT_LIGHT_TEMPERATURE_MIN) / 100;
    const double blend = (temperature % 100) / 100.0;
    vec3d result;
    for(int i = 0; i < 3; ++i) {
        const double whitepoint = blackbody_whitepoints[index][i] * (1.0 - blend) + blackbody_whitepoints[index + 1][i] * blend;
        result.v[i] = pow(whitepoint, 2.2);
    }
    return result;
}

static const gsr_color_chromaticities SRGB_CHROMATICITIES = {
    .red_x = 0.64f, .red_y = 0.33f,
    .green_x = 0.30f, .green_y = 0.60f,
    .blue_x = 0.15f, .blue_y = 0.06f,
    .white_x = 0.3127f, .white_y = 0.3290f
};

/* Bradford cone response matrices, the same values that kde plasma uses (kwin colorspace.cpp, chromaticAdaptationMatrix) */
static const mat3d BRADFORD = { .m = {
     0.8951,  0.2664, -0.1614,
    -0.7502,  1.7135,  0.0367,
     0.0389, -0.0685,  1.0296
}};

static const mat3d BRADFORD_INVERSE = { .m = {
     0.9869929, -0.1470543, 0.1599627,
     0.4323053,  0.5183603, 0.0492912,
    -0.0085287,  0.0400428, 0.9684867
}};

static vec3d chromaticity_to_xyz(double x, double y) {
    return (vec3d){ .v = { x / y, 1.0, (1.0 - x - y) / y } };
}

static vec3d mat3d_mul_vec3d(const mat3d *matrix, vec3d vec) {
    vec3d result;
    for(int i = 0; i < 3; ++i) {
        result.v[i] = matrix->m[i*3 + 0] * vec.v[0] + matrix->m[i*3 + 1] * vec.v[1] + matrix->m[i*3 + 2] * vec.v[2];
    }
    return result;
}

static mat3d mat3d_multiply(const mat3d *a, const mat3d *b) {
    mat3d result;
    for(int row = 0; row < 3; ++row) {
        for(int column = 0; column < 3; ++column) {
            result.m[row*3 + column] =
                a->m[row*3 + 0] * b->m[0*3 + column] +
                a->m[row*3 + 1] * b->m[1*3 + column] +
                a->m[row*3 + 2] * b->m[2*3 + column];
        }
    }
    return result;
}

static bool mat3d_invert(const mat3d *matrix, mat3d *result) {
    const double *m = matrix->m;
    const double c00 = m[4]*m[8] - m[5]*m[7];
    const double c01 = m[5]*m[6] - m[3]*m[8];
    const double c02 = m[3]*m[7] - m[4]*m[6];
    const double determinant = m[0]*c00 + m[1]*c01 + m[2]*c02;
    if(fabs(determinant) < 1e-12)
        return false;

    const double inverse_determinant = 1.0 / determinant;
    result->m[0] = c00 * inverse_determinant;
    result->m[1] = (m[2]*m[7] - m[1]*m[8]) * inverse_determinant;
    result->m[2] = (m[1]*m[5] - m[2]*m[4]) * inverse_determinant;
    result->m[3] = c01 * inverse_determinant;
    result->m[4] = (m[0]*m[8] - m[2]*m[6]) * inverse_determinant;
    result->m[5] = (m[2]*m[3] - m[0]*m[5]) * inverse_determinant;
    result->m[6] = c02 * inverse_determinant;
    result->m[7] = (m[1]*m[6] - m[0]*m[7]) * inverse_determinant;
    result->m[8] = (m[0]*m[4] - m[1]*m[3]) * inverse_determinant;
    return true;
}

static bool rgb_to_xyz_matrix(const gsr_color_chromaticities *chromaticities, mat3d *result) {
    const vec3d red = chromaticity_to_xyz(chromaticities->red_x, chromaticities->red_y);
    const vec3d green = chromaticity_to_xyz(chromaticities->green_x, chromaticities->green_y);
    const vec3d blue = chromaticity_to_xyz(chromaticities->blue_x, chromaticities->blue_y);
    const vec3d white = chromaticity_to_xyz(chromaticities->white_x, chromaticities->white_y);

    mat3d primaries;
    for(int i = 0; i < 3; ++i) {
        primaries.m[i*3 + 0] = red.v[i];
        primaries.m[i*3 + 1] = green.v[i];
        primaries.m[i*3 + 2] = blue.v[i];
    }

    mat3d primaries_inverse;
    if(!mat3d_invert(&primaries, &primaries_inverse))
        return false;

    const vec3d white_scale = mat3d_mul_vec3d(&primaries_inverse, white);
    for(int i = 0; i < 3; ++i) {
        result->m[i*3 + 0] = red.v[i] * white_scale.v[0];
        result->m[i*3 + 1] = green.v[i] * white_scale.v[1];
        result->m[i*3 + 2] = blue.v[i] * white_scale.v[2];
    }
    return true;
}

static mat3d bradford_chromatic_adaptation_matrix(vec3d source_whitepoint, vec3d destination_whitepoint) {
    const vec3d source_response = mat3d_mul_vec3d(&BRADFORD, source_whitepoint);
    const vec3d destination_response = mat3d_mul_vec3d(&BRADFORD, destination_whitepoint);
    mat3d scaled_bradford = BRADFORD;
    for(int i = 0; i < 3; ++i) {
        const double response_scale = destination_response.v[i] / source_response.v[i];
        for(int j = 0; j < 3; ++j) {
            scaled_bradford.m[i*3 + j] *= response_scale;
        }
    }
    return mat3d_multiply(&BRADFORD_INVERSE, &scaled_bradford);
}

/* Kde plasma applies night light by moving the whitepoint of the output to the night light whitepoint with bradford chromatic adaptation
   and dimming the output by the luminance of that whitepoint (kwin drm_output.cpp, applyNightLight - verified against kde plasma 6.7).
   The chromatic adaptation acts on the image in the color volume that the image is encoded in, so the inverse is conjugated by |container_primaries|,
   which is assumed to have a D65 whitepoint before the night light whitepoint change */
static void compute_inverse_night_light_matrix(int temperature, gsr_night_light_compensation compensation, const gsr_color_chromaticities *container_primaries, float inverse_matrix[9]) {
    memset(inverse_matrix, 0, sizeof(float) * 9);
    inverse_matrix[0] = 1.0f;
    inverse_matrix[4] = 1.0f;
    inverse_matrix[8] = 1.0f;

    const vec3d factors = night_light_channel_factors(temperature);

    gsr_color_chromaticities container_primaries_d65 = *container_primaries;
    container_primaries_d65.white_x = SRGB_CHROMATICITIES.white_x;
    container_primaries_d65.white_y = SRGB_CHROMATICITIES.white_y;

    mat3d srgb_to_xyz, container_to_xyz, xyz_to_container;
    if(!rgb_to_xyz_matrix(&SRGB_CHROMATICITIES, &srgb_to_xyz))
        return;
    if(!rgb_to_xyz_matrix(&container_primaries_d65, &container_to_xyz))
        return;
    if(!mat3d_invert(&container_to_xyz, &xyz_to_container))
        return;

    vec3d night_light_whitepoint = mat3d_mul_vec3d(&srgb_to_xyz, factors);
    const double luminance = fmax(night_light_whitepoint.v[1], 0.0001);
    for(int i = 0; i < 3; ++i) {
        night_light_whitepoint.v[i] /= luminance;
    }

    const vec3d d65_whitepoint = chromaticity_to_xyz(SRGB_CHROMATICITIES.white_x, SRGB_CHROMATICITIES.white_y);
    const mat3d inverse_adaptation = bradford_chromatic_adaptation_matrix(night_light_whitepoint, d65_whitepoint);

    const mat3d adaptation_in_container = mat3d_multiply(&xyz_to_container, &inverse_adaptation);
    const mat3d inverse_tint = mat3d_multiply(&adaptation_in_container, &container_to_xyz);

    const double luminance_scale = compensation == GSR_NIGHT_LIGHT_COMPENSATION_TINT ? 1.0 / luminance : 1.0;
    for(int i = 0; i < 9; ++i) {
        inverse_matrix[i] = inverse_tint.m[i] * luminance_scale;
    }
}

static bool dbus_get_night_light_property(DBusConnection *connection, const char *property_name, int expected_type, DBusBasicValue *value) {
    DBusMessage *message = dbus_message_new_method_call("org.kde.KWin.NightLight", "/org/kde/KWin/NightLight", "org.freedesktop.DBus.Properties", "Get");
    if(!message)
        return false;

    const char *interface_name = "org.kde.KWin.NightLight";
    dbus_message_append_args(message, DBUS_TYPE_STRING, &interface_name, DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, message, 500, NULL);
    dbus_message_unref(message);
    if(!reply)
        return false;

    bool success = false;
    DBusMessageIter iter;
    if(dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant_iter;
        dbus_message_iter_recurse(&iter, &variant_iter);
        if(dbus_message_iter_get_arg_type(&variant_iter) == expected_type) {
            dbus_message_iter_get_basic(&variant_iter, value);
            success = true;
        }
    }

    dbus_message_unref(reply);
    return success;
}

static int query_night_light_temperature(DBusConnection *connection) {
    DBusBasicValue temperature;
    if(!dbus_get_night_light_property(connection, "currentTemperature", DBUS_TYPE_UINT32, &temperature))
        return 0;

    if(temperature.u32 == 0 || temperature.u32 >= NIGHT_LIGHT_TEMPERATURE_NEUTRAL)
        return 0;

    return temperature.u32;
}

static void* night_light_poll_thread(void *userdata) {
    gsr_kde_night_light *self = userdata;

    DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
    if(!connection) {
        atomic_store(&self->temperature, 0);
        return NULL;
    }

    while(!atomic_load(&self->stop)) {
        atomic_store(&self->temperature, query_night_light_temperature(connection));
        for(int i = 0; i < NIGHT_LIGHT_POLL_SECONDS * 10 && !atomic_load(&self->stop); ++i) {
            usleep(100 * 1000);
        }
    }

    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    return NULL;
}

gsr_kde_night_light* gsr_kde_night_light_create(void) {
    gsr_kde_night_light *self = calloc(1, sizeof(gsr_kde_night_light));
    if(!self)
        return NULL;

    self->cached_temperature[0] = -1;
    self->cached_temperature[1] = -1;

    if(pthread_create(&self->thread, NULL, night_light_poll_thread, self) != 0) {
        free(self);
        return NULL;
    }

    self->thread_created = true;
    return self;
}

void gsr_kde_night_light_destroy(gsr_kde_night_light *self) {
    if(!self)
        return;

    if(self->thread_created) {
        atomic_store(&self->stop, true);
        pthread_join(self->thread, NULL);
    }
    free(self);
}

bool gsr_kde_night_light_get_inverse_matrix(gsr_kde_night_light *self, gsr_night_light_compensation compensation, const gsr_color_chromaticities *container_primaries, float inverse_matrix[9]) {
    if(!self)
        return false;

    const int temperature = atomic_load(&self->temperature);
    if(temperature <= 0)
        return false;

    if(!container_primaries)
        container_primaries = &SRGB_CHROMATICITIES;

    const int cache_index = compensation;
    if(self->cached_temperature[cache_index] != temperature || memcmp(&self->cached_container_primaries[cache_index], container_primaries, sizeof(*container_primaries)) != 0) {
        compute_inverse_night_light_matrix(temperature, compensation, container_primaries, self->cached_inverse_matrix[cache_index]);
        self->cached_temperature[cache_index] = temperature;
        self->cached_container_primaries[cache_index] = *container_primaries;
    }

    memcpy(inverse_matrix, self->cached_inverse_matrix[cache_index], sizeof(self->cached_inverse_matrix[cache_index]));
    return true;
}

#else /* GSR_DBUS */

#include <stddef.h>

gsr_kde_night_light* gsr_kde_night_light_create(void) {
    gsr_log(GSR_LOG_LEVEL_WARNING, "kde night light handling disabled because gsr was compiled without pipewire (which also disables dbus)");
    return NULL;
}

void gsr_kde_night_light_destroy(gsr_kde_night_light *self) {
    (void)self;
}

bool gsr_kde_night_light_get_inverse_matrix(gsr_kde_night_light *self, gsr_night_light_compensation compensation, const gsr_color_chromaticities *container_primaries, float inverse_matrix[9]) {
    (void)self;
    (void)compensation;
    (void)container_primaries;
    (void)inverse_matrix;
    return false;
}

#endif /* GSR_DBUS */
