#ifndef GSR_REPLAY_BUFFER_H
#define GSR_REPLAY_BUFFER_H

#include "../defs.h"
#include <stdbool.h>
#include <libavcodec/packet.h>

typedef struct gsr_replay_buffer gsr_replay_buffer;

typedef struct {
    size_t packet_index;
    size_t file_index;
} gsr_replay_buffer_iterator;

struct gsr_replay_buffer {
    void (*destroy)(gsr_replay_buffer *self);
    void (*destroy_at_exit)(gsr_replay_buffer *self);
    bool (*append)(gsr_replay_buffer *self, const AVPacket *av_packet, double timestamp);
    void (*clear)(gsr_replay_buffer *self);
    AVPacket* (*iterator_get_packet)(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator);
    /* The returned data should be free'd with free */
    uint8_t* (*iterator_get_packet_data)(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator);
    /* The clone has to be destroyed before the replay buffer it clones is destroyed */
    gsr_replay_buffer* (*clone)(gsr_replay_buffer *self);
    /* Returns {0, 0} if replay buffer is empty */
    gsr_replay_buffer_iterator (*find_packet_index_by_time_passed)(gsr_replay_buffer *self, int seconds);
    /* Returns {-1, 0} if not found */
    gsr_replay_buffer_iterator (*find_keyframe)(gsr_replay_buffer *self, gsr_replay_buffer_iterator start_iterator, int stream_index, bool invert_stream_index);
    bool (*iterator_next)(gsr_replay_buffer *self, gsr_replay_buffer_iterator *iterator);
};

gsr_replay_buffer* gsr_replay_buffer_create(gsr_replay_storage replay_storage, const char *replay_directory, double replay_buffer_time, size_t replay_buffer_num_packets);
void gsr_replay_buffer_destroy(gsr_replay_buffer *self);
/*
    Only frees the resources that the operating system doesn't free when the process exits, such as files,
    and leaves the memory to the operating system to free. Freeing a replay buffer that holds several gigabytes
    of data takes seconds, which is time wasted when the process is exiting anyway.
    |self| can't be used after this.
*/
void gsr_replay_buffer_destroy_at_exit(gsr_replay_buffer *self);

bool gsr_replay_buffer_append(gsr_replay_buffer *self, const AVPacket *av_packet, double timestamp);
void gsr_replay_buffer_clear(gsr_replay_buffer *self);
AVPacket* gsr_replay_buffer_iterator_get_packet(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator);
/* The returned data should be free'd with free */
uint8_t* gsr_replay_buffer_iterator_get_packet_data(gsr_replay_buffer *self, gsr_replay_buffer_iterator iterator);
/* The clone has to be destroyed before the replay buffer it clones is destroyed */
gsr_replay_buffer* gsr_replay_buffer_clone(gsr_replay_buffer *self);
/* Returns {0, 0} if replay buffer is empty */
gsr_replay_buffer_iterator gsr_replay_buffer_find_packet_index_by_time_passed(gsr_replay_buffer *self, int seconds);
/* Returns {-1, 0} if not found */
gsr_replay_buffer_iterator gsr_replay_buffer_find_keyframe(gsr_replay_buffer *self, gsr_replay_buffer_iterator start_iterator, int stream_index, bool invert_stream_index);
bool gsr_replay_buffer_iterator_next(gsr_replay_buffer *self, gsr_replay_buffer_iterator *iterator);

#endif /* GSR_REPLAY_BUFFER_H */