#include "../../include/capture/capture.h"
#include <assert.h>

int gsr_capture_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    assert(!cap->started);
    int res = cap->start(cap, capture_metadata);
    if(res == 0)
        cap->started = true;

    return res;
}

void gsr_capture_tick(gsr_capture *cap) {
    assert(cap->started);
    if(cap->tick)
        cap->tick(cap);
}

void gsr_capture_on_event(gsr_capture *cap, gsr_egl *egl) {
    if(cap->on_event)
        cap->on_event(cap, egl);
}

bool gsr_capture_should_stop(gsr_capture *cap, bool *err) {
    assert(cap->started);
    if(cap->should_stop)
        return cap->should_stop(cap, err);
    else
        return false;
}

int gsr_capture_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    assert(cap->started);
    return cap->capture(cap, capture_metadata, color_conversion);
}

bool gsr_capture_uses_external_image(gsr_capture *cap) {
    if(cap->uses_external_image)
        return cap->uses_external_image(cap);
    else
        return false;
}

bool gsr_capture_set_hdr_metadata(gsr_capture *cap, AVMasteringDisplayMetadata *mastering_display_metadata, AVContentLightMetadata *light_metadata) {
    if(cap->set_hdr_metadata)
        return cap->set_hdr_metadata(cap, mastering_display_metadata, light_metadata);
    else
        return false;
}

void gsr_capture_destroy(gsr_capture *cap) {
    cap->destroy(cap);
}
