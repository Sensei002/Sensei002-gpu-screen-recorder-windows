#include "../../include/replay_buffer/replay_buffer.h"
#include "../../include/replay_buffer/replay_buffer_ram.h"
#include "../../include/replay_buffer/replay_buffer_disk.h"

#include <stdlib.h>

gsr_replay_buffer* gsr_replay_buffer_create(gsr_replay_storage replay_storage, const char *replay_directory, double replay_buffer_time, size_t replay_buffer_num_packets) {
    gsr_replay_buffer *replay_buffer = NULL;
    switch(replay_storage) {
        case GSR_REPLAY_STORAGE_RAM:
            replay_buffer = gsr_replay_buffer_ram_create(replay_buffer_num_packets);
            break;
        case GSR_REPLAY_STORAGE_DISK:
            replay_buffer = gsr_replay_buffer_disk_create(replay_directory, replay_buffer_time);
            break;
    }
    return replay_buffer;
}

void gsr_replay_buffer_destroy(gsr_replay_buffer *self) {
    self->destroy(self);
    free(self);
}

void gsr_replay_buffer_destroy_at_exit(gsr_replay_buffer *self) {
    self->destroy_at_exit(self);
    /* |self| is intentionally not free'd, the operating system does that when the process exits */
}

bool gsr_replay_buffer_append(gsr_replay_buffer *self, const AVPacket *av_packet, double timestamp) {
    return self->append(self, av_packet, timestamp);
}

void gsr_replay_buffer_clear(gsr_replay_buffer *self) {
    self->clear(self);
}

AVPacket* gsr_replay_buffer_iterator_get_packet(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator) {
    return self->iterator_get_packet(self, iterator);
}

uint8_t* gsr_replay_buffer_iterator_get_packet_data(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator) {
    return self->iterator_get_packet_data(self, iterator);
}

gsr_replay_buffer* gsr_replay_buffer_clone(gsr_replay_buffer *self) {
    return self->clone(self);
}

gsr_replay_buffer_iterator gsr_replay_buffer_find_packet_index_by_time_passed(gsr_replay_buffer *self, int seconds) {
    return self->find_packet_index_by_time_passed(self, seconds);
}

gsr_replay_buffer_iterator gsr_replay_buffer_find_keyframe(gsr_replay_buffer *self, gsr_replay_buffer_iterator start_iterator, int stream_index, bool invert_stream_index) {
    return self->find_keyframe(self, start_iterator, stream_index, invert_stream_index);
}

bool gsr_replay_buffer_iterator_next(gsr_replay_buffer *self, gsr_replay_buffer_iterator *iterator) {
    return self->iterator_next(self, iterator);
}
