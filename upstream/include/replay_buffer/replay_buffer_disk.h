#ifndef GSR_REPLAY_BUFFER_DISK_H
#define GSR_REPLAY_BUFFER_DISK_H

#include "replay_buffer.h"
#include <limits.h>

#define GSR_REPLAY_BUFFER_CAPACITY_NUM_FILES 1024

typedef struct {
    AVPacket packet;
    size_t data_index;
    double timestamp;
} gsr_av_packet_disk;

typedef struct {
    size_t id;
    double start_timestamp;
    double end_timestamp;
    int ref_counter;
    int fd;
    
    gsr_av_packet_disk *packets;
    size_t capacity_num_packets;
    size_t num_packets;
} gsr_replay_buffer_file;

typedef struct {
    gsr_replay_buffer replay_buffer;
    double replay_buffer_time;

    size_t storage_counter;
    size_t storage_num_bytes_written;
    int storage_fd;
    gsr_replay_buffer_file *files[GSR_REPLAY_BUFFER_CAPACITY_NUM_FILES]; // GSR_REPLAY_BUFFER_CAPACITY_NUM_FILES * REPLAY_BUFFER_FILE_SIZE_BYTES = 256gb, should be enough for everybody
    size_t num_files;

    char replay_directory[PATH_MAX];

    bool owns_directory;
} gsr_replay_buffer_disk;

gsr_replay_buffer* gsr_replay_buffer_disk_create(const char *replay_directory, double replay_buffer_time);

#endif /* GSR_REPLAY_BUFFER_DISK_H */