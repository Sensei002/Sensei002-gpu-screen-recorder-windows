#ifndef GSR_REPLAY_BUFFER_RAM_H
#define GSR_REPLAY_BUFFER_RAM_H

#include "replay_buffer.h"

typedef struct {
    AVPacket packet;
    int ref_counter;
    double timestamp;
} gsr_av_packet_ram;

typedef struct {
    gsr_replay_buffer replay_buffer;
    gsr_av_packet_ram **packets;
    size_t capacity_num_packets;
    size_t num_packets;
    size_t index;
} gsr_replay_buffer_ram;

gsr_replay_buffer* gsr_replay_buffer_ram_create(size_t replay_buffer_num_packets);

#endif /* GSR_REPLAY_BUFFER_RAM_H */