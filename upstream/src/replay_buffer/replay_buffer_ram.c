#include "../../include/replay_buffer/replay_buffer_ram.h"
#include "../../include/utils.h"

#include <stdlib.h>
#include <assert.h>

#include <libavutil/mem.h>

static void gsr_replay_buffer_ram_set_impl_funcs(gsr_replay_buffer_ram *self);

static gsr_av_packet_ram* gsr_av_packet_ram_create(const AVPacket *av_packet, double timestamp) {
    gsr_av_packet_ram *self = malloc(sizeof(gsr_av_packet_ram));
    if(!self)
        return NULL;

    self->ref_counter = 1;
    self->packet = *av_packet;
    self->timestamp = timestamp;
    // Why are we doing this you ask? there is a ffmpeg bug that causes cpu usage to increase over time when you have
    // packets that are not being free'd until later. So we copy the packet data, free the packet and then reconstruct
    // the packet later on when we need it, to keep packets alive only for a short period.
    self->packet.data = av_memdup(av_packet->data, av_packet->size);
    if(!self->packet.data) {
        free(self);
        return NULL;
    }

    return self;
}

static gsr_av_packet_ram* gsr_av_packet_ram_ref(gsr_av_packet_ram *self) {
    if(self->ref_counter >= 1)
        ++self->ref_counter;
    return self;
}

static void gsr_av_packet_ram_free(gsr_av_packet_ram *self) {
    self->ref_counter = 0;
    if(self->packet.data) {
        av_free(self->packet.data);
        self->packet.data = NULL;
    }
    free(self);
}

static void gsr_av_packet_ram_unref(gsr_av_packet_ram *self) {
    if(self->ref_counter >= 1)
        --self->ref_counter;

    if(self->ref_counter <= 0)
        gsr_av_packet_ram_free(self);
}

static void gsr_replay_buffer_ram_destroy(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    for(size_t i = 0; i < self->num_packets; ++i) {
        if(self->packets[i]) {
            gsr_av_packet_ram_unref(self->packets[i]);
            self->packets[i] = NULL;
        }
    }
    self->num_packets = 0;

    if(self->packets) {
        free(self->packets);
        self->packets = NULL;
    }

    self->capacity_num_packets = 0;
    self->index = 0;
}

/* The replay buffer only holds memory, which the operating system frees when the process exits */
static void gsr_replay_buffer_ram_destroy_at_exit(gsr_replay_buffer *replay_buffer) {
    (void)replay_buffer;
}

static bool gsr_replay_buffer_ram_append(gsr_replay_buffer *replay_buffer, const AVPacket *av_packet, double timestamp) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    gsr_av_packet_ram *packet = gsr_av_packet_ram_create(av_packet, timestamp);
    if(!packet)
        return false;

    if(self->packets[self->index]) {
        gsr_av_packet_ram_unref(self->packets[self->index]);
        self->packets[self->index] = NULL;
    }
    self->packets[self->index] = packet;

    self->index = (self->index + 1) % self->capacity_num_packets;
    ++self->num_packets;
    if(self->num_packets > self->capacity_num_packets)
        self->num_packets = self->capacity_num_packets;

    return true;
}

/*
    The packets are not free'd here because that takes seconds when the replay buffer holds several gigabytes of data,
    which would block the thread that appends to the replay buffer for that long. The packets are instead left in place
    where they are no longer reachable and gsr_replay_buffer_ram_append free's them one by one as it overwrites them,
    which it already does when the ring buffer wraps around.
*/
static void gsr_replay_buffer_ram_clear(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    self->num_packets = 0;
    self->index = 0;
}

static gsr_av_packet_ram* gsr_replay_buffer_ram_get_packet_at_index(gsr_replay_buffer *replay_buffer, size_t index) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    assert(index < self->num_packets);
    size_t start_index = 0;
    if(self->num_packets < self->capacity_num_packets)
        start_index = self->num_packets - self->index;
    else
        start_index = self->index;

    const size_t offset = (start_index + index) % self->capacity_num_packets;
    return self->packets[offset];
}

static AVPacket* gsr_replay_buffer_ram_iterator_get_packet(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator iterator) {
    return &gsr_replay_buffer_ram_get_packet_at_index(replay_buffer, iterator.packet_index)->packet;
}

static uint8_t* gsr_replay_buffer_ram_iterator_get_packet_data(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator iterator) {
    (void)replay_buffer;
    (void)iterator;
    return NULL;
}

static gsr_replay_buffer* gsr_replay_buffer_ram_clone(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    gsr_replay_buffer_ram *destination = calloc(1, sizeof(gsr_replay_buffer_ram));
    if(!destination)
        return NULL;

    gsr_replay_buffer_ram_set_impl_funcs(destination);

    destination->capacity_num_packets = self->capacity_num_packets;
    destination->index = self->index;
    destination->packets = calloc(destination->capacity_num_packets, sizeof(gsr_av_packet_ram*));
    if(!destination->packets) {
        free(destination);
        return NULL;
    }

    destination->num_packets = self->num_packets;
    for(size_t i = 0; i < destination->num_packets; ++i) {
        destination->packets[i] = gsr_av_packet_ram_ref(self->packets[i]);
    }

    return (gsr_replay_buffer*)destination;
}

/* Binary search */
static gsr_replay_buffer_iterator gsr_replay_buffer_ram_find_packet_index_by_time_passed(gsr_replay_buffer *replay_buffer, int seconds) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;

    const double now = clock_get_monotonic_seconds();
    if(self->num_packets == 0) {
        return (gsr_replay_buffer_iterator){0, 0};
    }

    size_t lower_bound = 0;
    size_t upper_bound = self->num_packets;
    size_t index = 0;

    for(;;) {
        index = lower_bound + (upper_bound - lower_bound) / 2;
        const gsr_av_packet_ram *packet = gsr_replay_buffer_ram_get_packet_at_index(replay_buffer, index);
        const double time_passed_since_packet = now - packet->timestamp;
        if(time_passed_since_packet >= seconds) {
            if(lower_bound == index)
                break;
            lower_bound = index;
        } else {
            if(upper_bound == index)
                break;
            upper_bound = index;
        }
    }

    return (gsr_replay_buffer_iterator){index, 0};
}

static gsr_replay_buffer_iterator gsr_replay_buffer_ram_find_keyframe(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator start_iterator, int stream_index, bool invert_stream_index) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    size_t keyframe_index = (size_t)-1;
    for(size_t i = start_iterator.packet_index; i < self->num_packets; ++i) {
        const gsr_av_packet_ram *packet = gsr_replay_buffer_ram_get_packet_at_index(replay_buffer, i);
        if((packet->packet.flags & AV_PKT_FLAG_KEY) && (invert_stream_index ? packet->packet.stream_index != stream_index : packet->packet.stream_index == stream_index)) {
            keyframe_index = i;
            break;
        }
    }
    return (gsr_replay_buffer_iterator){keyframe_index, 0};
}

static bool gsr_replay_buffer_ram_iterator_next(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator *iterator) {
    gsr_replay_buffer_ram *self = (gsr_replay_buffer_ram*)replay_buffer;
    if(iterator->packet_index + 1 < self->num_packets) {
        ++iterator->packet_index;
        return true;
    } else {
        return false;
    }
}

static void gsr_replay_buffer_ram_set_impl_funcs(gsr_replay_buffer_ram *self) {
    self->replay_buffer.destroy = gsr_replay_buffer_ram_destroy;
    self->replay_buffer.destroy_at_exit = gsr_replay_buffer_ram_destroy_at_exit;
    self->replay_buffer.append = gsr_replay_buffer_ram_append;
    self->replay_buffer.clear = gsr_replay_buffer_ram_clear;
    self->replay_buffer.iterator_get_packet = gsr_replay_buffer_ram_iterator_get_packet;
    self->replay_buffer.iterator_get_packet_data = gsr_replay_buffer_ram_iterator_get_packet_data;
    self->replay_buffer.clone = gsr_replay_buffer_ram_clone;
    self->replay_buffer.find_packet_index_by_time_passed = gsr_replay_buffer_ram_find_packet_index_by_time_passed;
    self->replay_buffer.find_keyframe = gsr_replay_buffer_ram_find_keyframe;
    self->replay_buffer.iterator_next = gsr_replay_buffer_ram_iterator_next;
}

gsr_replay_buffer* gsr_replay_buffer_ram_create(size_t replay_buffer_num_packets) {
    assert(replay_buffer_num_packets > 0);
    gsr_replay_buffer_ram *replay_buffer = calloc(1, sizeof(gsr_replay_buffer_ram));
    if(!replay_buffer)
        return NULL;

    replay_buffer->capacity_num_packets = replay_buffer_num_packets;
    replay_buffer->num_packets = 0;
    replay_buffer->index = 0;
    replay_buffer->packets = calloc(replay_buffer->capacity_num_packets, sizeof(gsr_av_packet_ram*));
    if(!replay_buffer->packets) {
        gsr_replay_buffer_ram_destroy(&replay_buffer->replay_buffer);
        free(replay_buffer);
        return NULL;
    }

    gsr_replay_buffer_ram_set_impl_funcs(replay_buffer);
    return (gsr_replay_buffer*)replay_buffer;
}
