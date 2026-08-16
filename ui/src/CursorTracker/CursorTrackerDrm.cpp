#include "../../include/CursorTracker/CursorTrackerDrm.hpp"
#include "../../include/WindowUtils.hpp"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <mglpp/system/Rect.hpp>

namespace gsr {
    static const int MAX_CONNECTORS = 32;
    static const uint32_t plane_property_all = 0x3F;

    typedef enum {
        PLANE_PROPERTY_CRTC_X      = 1 << 0,
        PLANE_PROPERTY_CRTC_Y      = 1 << 1,
        PLANE_PROPERTY_CRTC_W      = 1 << 2,
        PLANE_PROPERTY_CRTC_H      = 1 << 3,
        PLANE_PROPERTY_CRTC_ID     = 1 << 4,
        PLANE_PROPERTY_TYPE_CURSOR = 1 << 5,
    } plane_property_mask;

    typedef struct {
        uint64_t crtc_id;
        mgl::vec2i size;
        bool vrr_enabled;
    } drm_connector;

    typedef struct {
        drm_connector connectors[MAX_CONNECTORS];
        int num_connectors;
        bool has_any_crtc_with_vrr_enabled;
    } drm_connectors;

    static bool rectangles_intersect(mgl::IntRect rect1, mgl::IntRect rect2) {
        return rect1.position.x < rect2.position.x + rect2.size.x && rect1.position.x + rect1.size.x > rect2.position.x &&
            rect1.position.y < rect2.position.y + rect2.size.y && rect1.position.y + rect1.size.y > rect2.position.y;
    }

    /* Returns plane_property_mask */
    static uint32_t plane_get_properties(int drm_fd, uint32_t plane_id, int *crtc_x, int *crtc_y, int *crtc_w, int *crtc_h, int *crtc_id) {
        *crtc_x = 0;
        *crtc_y = 0;
        *crtc_w = 0;
        *crtc_h = 0;
        *crtc_id = 0;

        uint32_t property_mask = 0;

        drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
        if(!props)
            return property_mask;

        for(uint32_t i = 0; i < props->count_props; ++i) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);
            if(!prop)
                continue;

            // SRC_* values are fixed 16.16 points
            const uint32_t type = prop->flags & (DRM_MODE_PROP_LEGACY_TYPE | DRM_MODE_PROP_EXTENDED_TYPE);
            if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_X") == 0) {
                *crtc_x = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_X;
            } else if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_Y") == 0) {
                *crtc_y = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_Y;
            } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "CRTC_W") == 0) {
                *crtc_w = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_W;
            } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "CRTC_H") == 0) {
                *crtc_h = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_H;
            } else if((type & DRM_MODE_PROP_OBJECT) && strcmp(prop->name, "CRTC_ID") == 0) {
                *crtc_id = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_ID;
            } else if((type & DRM_MODE_PROP_ENUM) && strcmp(prop->name, "type") == 0) {
                const uint64_t current_enum_value = props->prop_values[i];
                for(int j = 0; j < prop->count_enums; ++j) {
                    if(prop->enums[j].value == current_enum_value && strcmp(prop->enums[j].name, "Cursor") == 0) {
                        property_mask |= PLANE_PROPERTY_TYPE_CURSOR;
                        break;
                    }
                }
            }

            drmModeFreeProperty(prop);
        }

        drmModeFreeObjectProperties(props);
        return property_mask;
    }

    static bool get_drm_property_by_name(int drm_fd, drmModeObjectPropertiesPtr props, const char *name, uint64_t *result) {
        for(uint32_t i = 0; i < props->count_props; ++i) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);
            if(!prop)
                continue;

            if(strcmp(name, prop->name) == 0) {
                *result = props->prop_values[i];
                drmModeFreeProperty(prop);
                return true;
            }
            drmModeFreeProperty(prop);
        }
        return false;
    }

    static bool connector_get_property_by_name(int drm_fd, drmModeConnectorPtr props, const char *name, uint64_t *result) {
        drmModeObjectProperties properties;
        properties.count_props = (uint32_t)props->count_props;
        properties.props = props->props;
        properties.prop_values = props->prop_values;
        return get_drm_property_by_name(drm_fd, &properties, name, result);
    }

    // Note: this monitor name logic is kept in sync with gpu screen recorder
    static std::string get_monitor_name_from_crtc_id(int drm_fd, uint32_t crtc_id) {
        std::string result;
        drmModeResPtr resources = drmModeGetResources(drm_fd);
        if(!resources)
            return result;

        for(int i = 0; i < resources->count_connectors; ++i) {
            uint64_t connector_crtc_id = 0;
            drmModeConnectorPtr connector = drmModeGetConnectorCurrent(drm_fd, resources->connectors[i]);
            if(!connector)
                continue;

            const char *connection_name = drmModeGetConnectorTypeName(connector->connector_type);
            if(!connection_name)
                goto next;

            if(connector->connection != DRM_MODE_CONNECTED)
                goto next;

            if(connector_get_property_by_name(drm_fd, connector, "CRTC_ID", &connector_crtc_id) && connector_crtc_id == crtc_id) {
                result = connection_name;
                result += "-";
                result += std::to_string(connector->connector_type_id);
                drmModeFreeConnector(connector);
                break;
            }

            next:
            drmModeFreeConnector(connector);
        }

        drmModeFreeResources(resources);
        return result;
    }

    // Name is the crtc name. TODO: verify if this works on all wayland compositors
    static const Monitor* get_wayland_monitor_by_name(const std::vector<Monitor> &monitors, const std::string &name) {
        for(const Monitor &monitor : monitors) {
            if(monitor.name == name)
                return &monitor;
        }
        return nullptr;
    }

    /* Returns nullptr if not found */
    static drm_connector* get_drm_connector_by_crtc_id(drm_connectors *connectors, uint32_t crtc_id) {
        for(int i = 0; i < connectors->num_connectors; ++i) {
            if(connectors->connectors[i].crtc_id == crtc_id)
                return &connectors->connectors[i];
        }
        return nullptr;
    }

    static void get_drm_connectors(int drm_fd, drm_connectors *drm_connectors) {
        drm_connectors->num_connectors = 0;
        drm_connectors->has_any_crtc_with_vrr_enabled = false;

        drmModeResPtr resources = drmModeGetResources(drm_fd);
        if(!resources)
            return;

        for(int i = 0; i < resources->count_connectors && drm_connectors->num_connectors < MAX_CONNECTORS; ++i) {
            drmModeConnectorPtr connector = nullptr;
            drmModeCrtcPtr crtc = nullptr;

            connector = drmModeGetConnectorCurrent(drm_fd, resources->connectors[i]);
            if(!connector)
                continue;

            uint64_t crtc_id = 0;
            connector_get_property_by_name(drm_fd, connector, "CRTC_ID", &crtc_id);
            if(crtc_id == 0)
                goto next_connector;

            crtc = drmModeGetCrtc(drm_fd, crtc_id);
            if(!crtc)
                goto next_connector;

            drm_connectors->connectors[drm_connectors->num_connectors].crtc_id = crtc_id;
            drm_connectors->connectors[drm_connectors->num_connectors].size = mgl::vec2i{(int)crtc->width, (int)crtc->height};
            drm_connectors->connectors[drm_connectors->num_connectors].vrr_enabled = false;
            ++drm_connectors->num_connectors;

            next_connector:
            if(crtc)
                drmModeFreeCrtc(crtc);

            if(connector)
                drmModeFreeConnector(connector);
        }

        for(int i = 0; i < resources->count_crtcs; ++i) {
            drmModeCrtcPtr crtc = nullptr;
            drmModeObjectPropertiesPtr properties = nullptr;
            uint64_t vrr_enabled = 0;
            drm_connector *connector = nullptr;

            crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
            if(!crtc)
                continue;

            properties = drmModeObjectGetProperties(drm_fd, crtc->crtc_id, DRM_MODE_OBJECT_CRTC);
            if(!properties)
                goto next_crtc;

            get_drm_property_by_name(drm_fd, properties, "VRR_ENABLED", &vrr_enabled);

            connector = get_drm_connector_by_crtc_id(drm_connectors, crtc->crtc_id);
            if(!connector)
                goto next_crtc;

            if(vrr_enabled) {
                connector->vrr_enabled = true;
                drm_connectors->has_any_crtc_with_vrr_enabled = true;
            }

            next_crtc:
            if(properties)
                drmModeFreeObjectProperties(properties);

            if(crtc)
                drmModeFreeCrtc(crtc);
        }

        drmModeFreeResources(resources);
    }

    CursorTrackerDrm::CursorTrackerDrm(const char *card_path, struct wl_display *wayland_dpy) : wayland_dpy(wayland_dpy) {
        drm_fd = open(card_path, O_RDONLY);
        if(drm_fd <= 0) {
            fprintf(stderr, "Error: CursorTrackerDrm: failed to open %s\n", card_path);
            return;
        }

        drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    }

    CursorTrackerDrm::~CursorTrackerDrm() {
        if(drm_fd > 0)
            close(drm_fd);
    }

    void CursorTrackerDrm::update() {
        if(drm_fd <= 0)
            return;

        drm_connectors connectors;
        connectors.num_connectors = 0;
        connectors.has_any_crtc_with_vrr_enabled = false;
        get_drm_connectors(drm_fd, &connectors);

        drmModePlaneResPtr planes = drmModeGetPlaneResources(drm_fd);
        if(!planes)
            return;

        bool found_cursor = false;
        for(uint32_t i = 0; i < planes->count_planes; ++i) {
            drmModePlanePtr plane = nullptr;
            const drm_connector *connector = nullptr;

            int crtc_x = 0;
            int crtc_y = 0;
            int crtc_w = 0;
            int crtc_h = 0;
            int crtc_id = 0;
            uint32_t property_mask = 0;

            mgl::IntRect monitor_rect;
            mgl::IntRect cursor_rect;

            plane = drmModeGetPlane(drm_fd, planes->planes[i]);
            if(!plane)
                goto next;

            if(!plane->fb_id)
                goto next;

            property_mask = plane_get_properties(drm_fd, planes->planes[i], &crtc_x, &crtc_y, &crtc_w, &crtc_h, &crtc_id);
            if(property_mask != plane_property_all || crtc_id <= 0)
                goto next;

            connector = get_drm_connector_by_crtc_id(&connectors, crtc_id);
            if(!connector)
                goto next;

            monitor_rect = { mgl::vec2i(0, 0), connector->size };
            cursor_rect = { mgl::vec2i(crtc_x, crtc_y), mgl::vec2i(crtc_w, crtc_h) };

            if(rectangles_intersect(cursor_rect, cursor_rect)) {
                latest_cursor_position.x = crtc_x;
                latest_cursor_position.y = crtc_y;
                latest_crtc_id = crtc_id;
                found_cursor = true;
                drmModeFreePlane(plane);
                break;
            }

            next:
            drmModeFreePlane(plane);
        }

        // On kde plasma wayland (and possibly other wayland compositors) it uses a software cursor only for the monitors with vrr enabled.
        // In that case we cant know the cursor location and we instead want to fallback to getting focused monitor by using the hack of creating a window and getting the position.
        if(!found_cursor && latest_crtc_id > 0 && connectors.has_any_crtc_with_vrr_enabled)
            latest_crtc_id = -1;

        drmModeFreePlaneResources(planes);
    }

    std::optional<CursorInfo> CursorTrackerDrm::get_latest_cursor_info() {
        if(drm_fd <= 0 || latest_crtc_id == -1 || !wayland_dpy)
            return std::nullopt;

        std::string monitor_name = get_monitor_name_from_crtc_id(drm_fd, latest_crtc_id);
        if(monitor_name.empty())
            return std::nullopt;

        const std::vector<Monitor> wayland_monitors = get_monitors_wayland(wayland_dpy);
        const Monitor *wayland_monitor = get_wayland_monitor_by_name(wayland_monitors, monitor_name);
        if(!wayland_monitor)
            return std::nullopt;

        return CursorInfo{ wayland_monitor->position + latest_cursor_position, std::move(monitor_name) };
    }
}