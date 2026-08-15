#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../kms_shared.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <drm_fourcc.h>

#define MAX_CONNECTORS 32

typedef struct {
    int drmfd;
} gsr_drm;

typedef struct {
    uint32_t connector_id;
    uint64_t crtc_id;
    uint64_t hdr_metadata_blob_id;
} connector_crtc_pair;

typedef struct {
    connector_crtc_pair maps[MAX_CONNECTORS];
    int num_maps;
} connector_to_crtc_map;

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static int count_num_fds(const gsr_kms_response *response) {
    int num_fds = 0;
    for(int i = 0; i < response->num_items; ++i) {
        num_fds += response->items[i].num_dma_bufs;
    }
    return num_fds;
}

static int send_msg_to_client(int client_fd, gsr_kms_response *response) {
    struct iovec iov;
    iov.iov_base = response;
    iov.iov_len = sizeof(*response);

    struct msghdr response_message = {0};
    response_message.msg_iov = &iov;
    response_message.msg_iovlen = 1;

    const int num_fds = count_num_fds(response);
    char cmsgbuf[CMSG_SPACE(sizeof(int) * max_int(1, num_fds))];
    memset(cmsgbuf, 0, sizeof(cmsgbuf));

    if(num_fds > 0) {
        response_message.msg_control = cmsgbuf;
        response_message.msg_controllen = sizeof(cmsgbuf);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&response_message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fds);

        int *fds = (int*)CMSG_DATA(cmsg);
        int fd_index = 0;
        for(int i = 0; i < response->num_items; ++i) {
            for(int j = 0; j < response->items[i].num_dma_bufs; ++j) {
                fds[fd_index++] = response->items[i].dma_buf[j].fd;
            }
        }

        response_message.msg_controllen = cmsg->cmsg_len;
    }

    return sendmsg(client_fd, &response_message, 0);
}

static int recv_msg_from_client(int client_fd, gsr_kms_request *request) {
    struct iovec iov;
    iov.iov_base = request;
    iov.iov_len = sizeof(*request);

    struct msghdr response_message = {0};
    response_message.msg_iov = &iov;
    response_message.msg_iovlen = 1;

    char cmsgbuf[CMSG_SPACE(sizeof(int) * 1)];
    memset(cmsgbuf, 0, sizeof(cmsgbuf));
    response_message.msg_control = cmsgbuf;
    response_message.msg_controllen = sizeof(cmsgbuf);

    int res = recvmsg(client_fd, &response_message, MSG_WAITALL);
    if(res <= 0)
        return res;

    if(request->new_connection_fd > 0) {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&response_message);
        if(cmsg) {
            int *fds = (int*)CMSG_DATA(cmsg);
            request->new_connection_fd = fds[0];
        } else {
            if(request->new_connection_fd > 0) {
                close(request->new_connection_fd);
                request->new_connection_fd = 0;
            }
        }
    }

    return res;
}

static bool connector_get_property_by_name(int drmfd, drmModeConnectorPtr props, const char *name, uint64_t *result) {
    for(int i = 0; i < props->count_props; ++i) {
        drmModePropertyPtr prop = drmModeGetProperty(drmfd, props->props[i]);
        if(prop) {
            if(strcmp(name, prop->name) == 0) {
                *result = props->prop_values[i];
                drmModeFreeProperty(prop);
                return true;
            }
            drmModeFreeProperty(prop);
        }
    }
    return false;
}

typedef enum {
    PLANE_PROPERTY_X          = 1 << 0,
    PLANE_PROPERTY_Y          = 1 << 1,
    PLANE_PROPERTY_SRC_X      = 1 << 2,
    PLANE_PROPERTY_SRC_Y      = 1 << 3,
    PLANE_PROPERTY_SRC_W      = 1 << 4,
    PLANE_PROPERTY_SRC_H      = 1 << 5,
    PLANE_PROPERTY_IS_CURSOR  = 1 << 6,
    PLANE_PROPERTY_IS_PRIMARY = 1 << 7,
    PLANE_PROPERTY_ROTATION   = 1 << 8,
    PLANE_PROPERTY_W          = 1 << 9,
    PLANE_PROPERTY_H          = 1 << 10,
    PLANE_PROPERTY_IS_OVERLAY = 1 << 11,
    PLANE_PROPERTY_ZPOS       = 1 << 12,
} plane_property_mask;

typedef struct {
    int x, y, w, h;             /* Region on the crtc (monitor) where the plane is displayed (CRTC_* properties) */
    int src_x, src_y, src_w, src_h; /* Region in the framebuffer that is displayed (SRC_* properties) */
    int zpos;
    gsr_kms_rotation rotation;
} plane_properties;

/* Returns plane_property_mask */
static uint32_t plane_get_properties(int drmfd, uint32_t plane_id, plane_properties *properties) {
    memset(properties, 0, sizeof(*properties));
    properties->rotation = KMS_ROT_0;

    plane_property_mask property_mask = 0;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drmfd, plane_id, DRM_MODE_OBJECT_PLANE);
    if(!props)
        return property_mask;

    // TODO: Dont do this every frame
    for(uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyPtr prop = drmModeGetProperty(drmfd, props->props[i]);
        if(!prop)
            continue;

        // SRC_* values are fixed 16.16 points
        const uint32_t type = prop->flags & (DRM_MODE_PROP_LEGACY_TYPE | DRM_MODE_PROP_EXTENDED_TYPE);
        if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_X") == 0) {
            properties->x = (int)props->prop_values[i];
            property_mask |= PLANE_PROPERTY_X;
        } else if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_Y") == 0) {
            properties->y = (int)props->prop_values[i];
            property_mask |= PLANE_PROPERTY_Y;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "CRTC_W") == 0) {
            properties->w = (int)props->prop_values[i];
            property_mask |= PLANE_PROPERTY_W;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "CRTC_H") == 0) {
            properties->h = (int)props->prop_values[i];
            property_mask |= PLANE_PROPERTY_H;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "SRC_X") == 0) {
            properties->src_x = (int)(props->prop_values[i] >> 16);
            property_mask |= PLANE_PROPERTY_SRC_X;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "SRC_Y") == 0) {
            properties->src_y = (int)(props->prop_values[i] >> 16);
            property_mask |= PLANE_PROPERTY_SRC_Y;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "SRC_W") == 0) {
            properties->src_w = (int)(props->prop_values[i] >> 16);
            property_mask |= PLANE_PROPERTY_SRC_W;
        } else if((type & DRM_MODE_PROP_RANGE) && strcmp(prop->name, "SRC_H") == 0) {
            properties->src_h = (int)(props->prop_values[i] >> 16);
            property_mask |= PLANE_PROPERTY_SRC_H;
        } else if(((type & DRM_MODE_PROP_RANGE) || (type & DRM_MODE_PROP_SIGNED_RANGE)) && (strcmp(prop->name, "zpos") == 0 || strcmp(prop->name, "ZPOS") == 0)) {
            if(type & DRM_MODE_PROP_SIGNED_RANGE)
                properties->zpos = (int)(int64_t)props->prop_values[i];
            else
                properties->zpos = (int)props->prop_values[i];
            property_mask |= PLANE_PROPERTY_ZPOS;
        } else if((type & DRM_MODE_PROP_ENUM) && strcmp(prop->name, "type") == 0) {
            const uint64_t current_enum_value = props->prop_values[i];
            for(int j = 0; j < prop->count_enums; ++j) {
                if(prop->enums[j].value == current_enum_value && strcmp(prop->enums[j].name, "Primary") == 0) {
                    property_mask |= PLANE_PROPERTY_IS_PRIMARY;
                    break;
                } else if(prop->enums[j].value == current_enum_value && strcmp(prop->enums[j].name, "Cursor") == 0) {
                    property_mask |= PLANE_PROPERTY_IS_CURSOR;
                    break;
                } else if(prop->enums[j].value == current_enum_value && strcmp(prop->enums[j].name, "Overlay") == 0) {
                    property_mask |= PLANE_PROPERTY_IS_OVERLAY;
                    break;
                }
            }
        } else if((type & DRM_MODE_PROP_BITMASK) && strcmp(prop->name, "rotation") == 0) {
            const uint64_t rotation_bitmask = props->prop_values[i];
            properties->rotation = KMS_ROT_0;
            if(rotation_bitmask & 2)
                properties->rotation = (properties->rotation + KMS_ROT_90) % 4;
            if(rotation_bitmask & 4)
                properties->rotation = (properties->rotation + KMS_ROT_180) % 4;
            if(rotation_bitmask & 8)
                properties->rotation = (properties->rotation + KMS_ROT_270) % 4;
        }

        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);
    return property_mask;
}

/* Returns NULL if not found */
static const connector_crtc_pair* get_connector_pair_by_crtc_id(const connector_to_crtc_map *c2crtc_map, uint32_t crtc_id) {
    for(int i = 0; i < c2crtc_map->num_maps; ++i) {
        if(c2crtc_map->maps[i].crtc_id == crtc_id)
            return &c2crtc_map->maps[i];
    }
    return NULL;
}

static void map_crtc_to_connector_ids(gsr_drm *drm, connector_to_crtc_map *c2crtc_map) {
    c2crtc_map->num_maps = 0;
    drmModeResPtr resources = drmModeGetResources(drm->drmfd);
    if(!resources)
        return;

    for(int i = 0; i < resources->count_connectors && c2crtc_map->num_maps < MAX_CONNECTORS; ++i) {
        drmModeConnectorPtr connector = drmModeGetConnectorCurrent(drm->drmfd, resources->connectors[i]);
        if(!connector)
            continue;

        uint64_t crtc_id = 0;
        connector_get_property_by_name(drm->drmfd, connector, "CRTC_ID", &crtc_id);

        uint64_t hdr_output_metadata_blob_id = 0;
        connector_get_property_by_name(drm->drmfd, connector, "HDR_OUTPUT_METADATA", &hdr_output_metadata_blob_id);

        c2crtc_map->maps[c2crtc_map->num_maps].connector_id = connector->connector_id;
        c2crtc_map->maps[c2crtc_map->num_maps].crtc_id = crtc_id;
        c2crtc_map->maps[c2crtc_map->num_maps].hdr_metadata_blob_id = hdr_output_metadata_blob_id;
        ++c2crtc_map->num_maps;

        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources);
}

static void drm_mode_cleanup_handles(int drmfd, drmModeFB2Ptr drmfb) {
    for(int i = 0; i < 4; ++i) {
        if(!drmfb->handles[i])
            continue;

        bool already_closed = false;
        for(int j = 0; j < i; ++j) {
            if(drmfb->handles[i] == drmfb->handles[j]) {
                already_closed = true;
                break;
            }
        }

        if(already_closed)
            continue;

        drmCloseBufferHandle(drmfd, drmfb->handles[i]);
    }
}

static bool get_hdr_metadata(int drm_fd, uint64_t hdr_metadata_blob_id, struct hdr_output_metadata *hdr_metadata) {
    drmModePropertyBlobPtr hdr_metadata_blob = drmModeGetPropertyBlob(drm_fd, hdr_metadata_blob_id);
    if(!hdr_metadata_blob)
        return false;

    if(hdr_metadata_blob->length >= sizeof(struct hdr_output_metadata))
        *hdr_metadata = *(struct hdr_output_metadata*)hdr_metadata_blob->data;

    drmModeFreePropertyBlob(hdr_metadata_blob);
    return true;
}

/* Returns the number of drm handles that we managed to get */
static int drm_prime_handles_to_fds(gsr_drm *drm, drmModeFB2Ptr drmfb, int *fb_fds) {
    for(int i = 0; i < GSR_KMS_MAX_DMA_BUFS; ++i) {
        if(!drmfb->handles[i])
            return i;

        const int ret = drmPrimeHandleToFD(drm->drmfd, drmfb->handles[i], O_RDONLY, &fb_fds[i]);
        if(ret != 0 || fb_fds[i] == -1)
            return i;
    }
    return GSR_KMS_MAX_DMA_BUFS;
}

static int kms_get_fb(gsr_drm *drm, gsr_kms_response *response) {
    int result = -1;

    response->result = KMS_RESULT_OK;
    response->err_msg[0] = '\0';
    response->num_items = 0;

    connector_to_crtc_map c2crtc_map;
    c2crtc_map.num_maps = 0;
    map_crtc_to_connector_ids(drm, &c2crtc_map);

    drmModePlaneResPtr planes = drmModeGetPlaneResources(drm->drmfd);
    if(!planes) {
        fprintf(stderr, "kms server error: failed to get plane resources, error: %s\n", strerror(errno));
        goto done;
    }

    for(uint32_t i = 0; i < planes->count_planes && response->num_items < GSR_KMS_MAX_ITEMS; ++i) {
        drmModePlanePtr plane = NULL;
        drmModeFB2Ptr drmfb = NULL;

        plane = drmModeGetPlane(drm->drmfd, planes->planes[i]);
        if(!plane) {
            response->result = KMS_RESULT_FAILED_TO_GET_PLANE;
            snprintf(response->err_msg, sizeof(response->err_msg), "failed to get drm plane with id %u, error: %s\n", planes->planes[i], strerror(errno));
            fprintf(stderr, "kms server error: %s\n", response->err_msg);
            goto next;
        }

        if(!plane->fb_id)
            goto next;

        drmfb = drmModeGetFB2(drm->drmfd, plane->fb_id);
        if(!drmfb) {
            // Commented out for now because we get here if the cursor is moved to another monitor and we dont care about the cursor
            //response->result = KMS_RESULT_FAILED_TO_GET_PLANE;
            //snprintf(response->err_msg, sizeof(response->err_msg), "drmModeGetFB2 failed, error: %s", strerror(errno));
            //fprintf(stderr, "kms server error: %s\n", response->err_msg);
            goto next;
        }

        if(!drmfb->handles[0]) {
            response->result = KMS_RESULT_FAILED_TO_GET_PLANE;
            snprintf(response->err_msg, sizeof(response->err_msg), "drmfb handle is NULL");
            fprintf(stderr, "kms server error: %s\n", response->err_msg);
            goto cleanup_handles;
        }

        // TODO: Check if dimensions have changed by comparing width and height to previous time this was called.
        // TODO: Support other plane formats than rgb (with multiple planes, such as direct YUV420 on wayland).

        plane_properties properties;
        const uint32_t property_mask = plane_get_properties(drm->drmfd, plane->plane_id, &properties);
        if(!(property_mask & (PLANE_PROPERTY_IS_PRIMARY | PLANE_PROPERTY_IS_CURSOR | PLANE_PROPERTY_IS_OVERLAY)))
            goto cleanup_handles;

        gsr_kms_plane_type plane_type = KMS_PLANE_TYPE_OVERLAY;
        if(property_mask & PLANE_PROPERTY_IS_PRIMARY)
            plane_type = KMS_PLANE_TYPE_PRIMARY;
        else if(property_mask & PLANE_PROPERTY_IS_CURSOR)
            plane_type = KMS_PLANE_TYPE_CURSOR;

        /* These properties are not available when the drm driver doesn't support atomic modesetting */
        if(!(property_mask & PLANE_PROPERTY_SRC_W))
            properties.src_w = drmfb->width;
        if(!(property_mask & PLANE_PROPERTY_SRC_H))
            properties.src_h = drmfb->height;
        if(!(property_mask & PLANE_PROPERTY_W))
            properties.w = properties.src_w;
        if(!(property_mask & PLANE_PROPERTY_H))
            properties.h = properties.src_h;

        /* Not all drm drivers support zpos. In that case assume that the planes are stacked in the order primary, overlay, cursor (bottom to top) */
        if(!(property_mask & PLANE_PROPERTY_ZPOS)) {
            switch(plane_type) {
                case KMS_PLANE_TYPE_PRIMARY:
                    properties.zpos = 0;
                    break;
                case KMS_PLANE_TYPE_OVERLAY:
                    properties.zpos = 1;
                    break;
                case KMS_PLANE_TYPE_CURSOR:
                    properties.zpos = 2;
                    break;
            }
        }

        int fb_fds[GSR_KMS_MAX_DMA_BUFS];
        const int num_fb_fds = drm_prime_handles_to_fds(drm, drmfb, fb_fds);
        if(num_fb_fds == 0) {
            response->result = KMS_RESULT_FAILED_TO_GET_PLANE;
            snprintf(response->err_msg, sizeof(response->err_msg), "failed to get fd from drm handle, error: %s", strerror(errno));
            fprintf(stderr, "kms server error: %s\n", response->err_msg);
            goto cleanup_handles;
        }

        const int item_index = response->num_items;

        const connector_crtc_pair *crtc_pair = get_connector_pair_by_crtc_id(&c2crtc_map, plane->crtc_id);
        if(crtc_pair && crtc_pair->hdr_metadata_blob_id) {
            response->items[item_index].has_hdr_metadata = get_hdr_metadata(drm->drmfd, crtc_pair->hdr_metadata_blob_id, &response->items[item_index].hdr_metadata);
        } else {
            response->items[item_index].has_hdr_metadata = false;
        }

        for(int j = 0; j < num_fb_fds; ++j) {
            response->items[item_index].dma_buf[j].fd = fb_fds[j];
            response->items[item_index].dma_buf[j].pitch = drmfb->pitches[j];
            response->items[item_index].dma_buf[j].offset = drmfb->offsets[j];
        }
        response->items[item_index].num_dma_bufs = num_fb_fds;

        response->items[item_index].width = drmfb->width;
        response->items[item_index].height = drmfb->height;
        response->items[item_index].pixel_format = drmfb->pixel_format;
        response->items[item_index].modifier = drmfb->flags & DRM_MODE_FB_MODIFIERS ? drmfb->modifier : DRM_FORMAT_MOD_INVALID;
        response->items[item_index].connector_id = crtc_pair ? crtc_pair->connector_id : 0;
        response->items[item_index].rotation = properties.rotation;
        response->items[item_index].plane_type = plane_type;
        response->items[item_index].src_x = properties.src_x;
        response->items[item_index].src_y = properties.src_y;
        response->items[item_index].src_w = properties.src_w;
        response->items[item_index].src_h = properties.src_h;
        response->items[item_index].dst_x = properties.x;
        response->items[item_index].dst_y = properties.y;
        response->items[item_index].dst_w = properties.w;
        response->items[item_index].dst_h = properties.h;
        response->items[item_index].zpos = properties.zpos;
        ++response->num_items;

        cleanup_handles:
        drm_mode_cleanup_handles(drm->drmfd, drmfb);

        next:
        if(drmfb)
            drmModeFreeFB2(drmfb);
        if(plane)
            drmModeFreePlane(plane);
    }

    done:

    if(planes)
        drmModeFreePlaneResources(planes);

    if(response->num_items > 0)
        response->result = KMS_RESULT_OK;

    if(response->result == KMS_RESULT_OK) {
        result = 0;
    } else {
        for(int i = 0; i < response->num_items; ++i) {
            for(int j = 0; j < response->items[i].num_dma_bufs; ++j) {
                gsr_kms_response_dma_buf *dma_buf = &response->items[i].dma_buf[j];
                if(dma_buf->fd > 0) {
                    close(dma_buf->fd);
                    dma_buf->fd = -1;
                }
            }
            response->items[i].num_dma_bufs = 0;
        }
        response->num_items = 0;
    }

    return result;
}

static double clock_get_monotonic_seconds(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 0.000000001;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C

    int res = 0;
    int socket_fd = 0;
    gsr_drm drm;
    drm.drmfd = 0;

    if(argc != 3) {
        fprintf(stderr, "usage: gsr-kms-server <domain_socket_path> <card_path>\n");
        return 1;
    }

    const char *domain_socket_path = argv[1];
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(socket_fd == -1) {
        fprintf(stderr, "kms server error: failed to create socket, error: %s\n", strerror(errno));
        return 2;
    }

    const char *card_path = argv[2];

    drm.drmfd = open(card_path, O_RDONLY);
    if(drm.drmfd < 0) {
        fprintf(stderr, "kms server error: failed to open %s, error: %s", card_path, strerror(errno));
        res = 2;
        goto done;
    }

    if(drmSetClientCap(drm.drmfd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        fprintf(stderr, "kms server error: drmSetClientCap DRM_CLIENT_CAP_UNIVERSAL_PLANES failed, error: %s\n", strerror(errno));
        res = 2;
        goto done;
    }

    if(drmSetClientCap(drm.drmfd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        fprintf(stderr, "kms server warning: drmSetClientCap DRM_CLIENT_CAP_ATOMIC failed, error: %s. The wrong monitor may be captured as a result\n", strerror(errno));
    }

    fprintf(stderr, "kms server info: connecting to the client\n");
    bool connected = false;
    const double connect_timeout_sec = 5.0;
    const double start_time = clock_get_monotonic_seconds();
    while(clock_get_monotonic_seconds() - start_time < connect_timeout_sec) {
        struct sockaddr_un remote_addr = {0};
        remote_addr.sun_family = AF_UNIX;
        snprintf(remote_addr.sun_path, sizeof(remote_addr.sun_path), "%s", domain_socket_path);
        // TODO: Check if parent disconnected
        if(connect(socket_fd, (struct sockaddr*)&remote_addr, sizeof(remote_addr.sun_family) + strlen(remote_addr.sun_path)) == -1) {
            if(errno == ECONNREFUSED || errno == ENOENT) {
                goto next;
            } else if(errno == EISCONN) {
                connected = true;
                break;
            }

            fprintf(stderr, "kms server error: connect failed, error: %s (%d)\n", strerror(errno), errno);
            res = 2;
            goto done;
        }

        next:
        usleep(30 * 1000); // 30 milliseconds
    }

    if(connected) {
        fprintf(stderr, "kms server info: connected to the client\n");
    } else {
        fprintf(stderr, "kms server error: failed to connect to the client in %f seconds\n", connect_timeout_sec);
        res = 2;
        goto done;
    }

    for(;;) {
        gsr_kms_request request;
        request.version = 0;
        request.type = -1;
        request.new_connection_fd = 0;

        const int recv_res = recv_msg_from_client(socket_fd, &request);
        if(recv_res == 0) {
            fprintf(stderr, "kms server info: kms client shutdown, shutting down the server\n");
            res = 3;
            goto done;
        } else if(recv_res == -1) {
            const int err = errno;
            fprintf(stderr, "kms server error: failed to read all data in client request (error: %s), ignoring\n", strerror(err));
            if(err == EBADF) {
                fprintf(stderr, "kms server error: invalid client fd, shutting down the server\n");
                res = 3;
                goto done;
            }
            continue;
        }

        if(request.version != GSR_KMS_PROTOCOL_VERSION) {
            fprintf(stderr, "kms server error: expected gpu screen recorder protocol version to be %u, but it's %u. please reinstall gpu screen recorder\n", GSR_KMS_PROTOCOL_VERSION, request.version);
            /*
            if(request.new_connection_fd > 0)
                close(request.new_connection_fd);
            */
            continue;
        }

        switch(request.type) {
            case KMS_REQUEST_TYPE_REPLACE_CONNECTION: {
                gsr_kms_response response;
                response.version = GSR_KMS_PROTOCOL_VERSION;
                response.num_items = 0;

                if(request.new_connection_fd > 0) {
                    if(socket_fd > 0)
                        close(socket_fd);
                    socket_fd = request.new_connection_fd;

                    response.result = KMS_RESULT_OK;
                    if(send_msg_to_client(socket_fd, &response) == -1)
                        fprintf(stderr, "kms server error: failed to respond to client KMS_REQUEST_TYPE_REPLACE_CONNECTION request\n");
                } else {
                    response.result = KMS_RESULT_INVALID_REQUEST;
                    snprintf(response.err_msg, sizeof(response.err_msg), "received invalid connection fd");
                    fprintf(stderr, "kms server error: %s\n", response.err_msg);
                    if(send_msg_to_client(socket_fd, &response) == -1)
                        fprintf(stderr, "kms server error: failed to respond to client request\n");
                }

                break;
            }
            case KMS_REQUEST_TYPE_GET_KMS: {
                gsr_kms_response response;
                response.version = GSR_KMS_PROTOCOL_VERSION;
                response.num_items = 0;
                
                if(kms_get_fb(&drm, &response) == 0) {
                    if(send_msg_to_client(socket_fd, &response) == -1)
                        fprintf(stderr, "kms server error: failed to respond to client KMS_REQUEST_TYPE_GET_KMS request\n");
                } else {
                    if(send_msg_to_client(socket_fd, &response) == -1)
                        fprintf(stderr, "kms server error: failed to respond to client KMS_REQUEST_TYPE_GET_KMS request\n");
                }

                for(int i = 0; i < response.num_items; ++i) {
                    for(int j = 0; j < response.items[i].num_dma_bufs; ++j) {
                        gsr_kms_response_dma_buf *dma_buf = &response.items[i].dma_buf[j];
                        if(dma_buf->fd > 0) {
                            close(dma_buf->fd);
                            dma_buf->fd = -1;
                        }
                    }
                    response.items[i].num_dma_bufs = 0;
                }
                response.num_items = 0;

                break;
            }
            default: {
                gsr_kms_response response;
                response.version = GSR_KMS_PROTOCOL_VERSION;
                response.result = KMS_RESULT_INVALID_REQUEST;
                response.num_items = 0;

                snprintf(response.err_msg, sizeof(response.err_msg), "invalid request type %d, expected %d (%s)", request.type, KMS_REQUEST_TYPE_GET_KMS, "KMS_REQUEST_TYPE_GET_KMS");
                fprintf(stderr, "kms server error: %s\n", response.err_msg);
                if(send_msg_to_client(socket_fd, &response) == -1)
                    fprintf(stderr, "kms server error: failed to respond to client request\n");

                break;
            }
        }
    }

    done:
    if(drm.drmfd > 0)
        close(drm.drmfd);
    if(socket_fd > 0)
        close(socket_fd);
    return res;
}
