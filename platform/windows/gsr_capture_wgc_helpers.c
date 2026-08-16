/* gsr_capture_wgc_helpers.c — pure logic for the WGC capture backend.
 *
 * Phase 5 deliverable. Everything here is deterministic and free of Win32 /
 * WinRT calls so tests/platform-test can exercise it headless on CI; the
 * C++/WinRT backend (gsr_capture_wgc.cpp) calls these for the decisions
 * that must be testable without a display session.
 */
#include "../../platform/include/capture.h"

gsr_platform_wgc_rotation gsr_platform_wgc_rotation_from_monitor(int rotation_degrees) {
    switch(rotation_degrees) {
    case 90:  return GSR_PLATFORM_WGC_ROT_90;
    case 180: return GSR_PLATFORM_WGC_ROT_180;
    case 270: return GSR_PLATFORM_WGC_ROT_270;
    default:  return GSR_PLATFORM_WGC_ROT_0;
    }
}

uint32_t gsr_platform_wgc_flip_from_source(uint32_t source_flip) {
    uint32_t flip = GSR_PLATFORM_WGC_FLIP_NONE;
    if(source_flip & (1u << 0)) /* upstream GSR_FLIP_HORIZONTAL */
        flip |= GSR_PLATFORM_WGC_FLIP_HORIZONTAL;
    if(source_flip & (1u << 1)) /* upstream GSR_FLIP_VERTICAL */
        flip |= GSR_PLATFORM_WGC_FLIP_VERTICAL;
    return flip;
}

gsr_platform_wgc_source_color gsr_platform_wgc_source_color_from_pixel_format(uint32_t dxgi_format) {
    /* DXGI_FORMAT_B8G8R8A8_UNORM = 87 (WGC's Direct3DPixelFormat
       B8G8R8A8UIntNormalized maps to it). */
    if(dxgi_format == 87)
        return GSR_PLATFORM_WGC_SOURCE_BGR;
    return GSR_PLATFORM_WGC_SOURCE_RGB;
}

gsr_platform_wgc_device gsr_platform_wgc_select_device(bool hardware_available) {
    return hardware_available ? GSR_PLATFORM_WGC_DEVICE_HARDWARE : GSR_PLATFORM_WGC_DEVICE_WARP;
}

void gsr_platform_wgc_damage_init(gsr_platform_wgc_damage *self) {
    self->frame_pending = false;
    self->consumed = false;
}

void gsr_platform_wgc_damage_on_frame(gsr_platform_wgc_damage *self) {
    self->frame_pending = true;
    self->consumed = false;
}

bool gsr_platform_wgc_damage_is_damaged(const gsr_platform_wgc_damage *self) {
    return self->frame_pending && !self->consumed;
}

void gsr_platform_wgc_damage_consume(gsr_platform_wgc_damage *self) {
    self->consumed = true;
}
