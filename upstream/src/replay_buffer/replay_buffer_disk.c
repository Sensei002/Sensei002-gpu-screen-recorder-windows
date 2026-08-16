#include "../../include/replay_buffer/replay_buffer_disk.h"
#include "../../include/log.h"
#include "../../include/utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#define REPLAY_BUFFER_FILE_SIZE_BYTES 1024 * 1024 * 256 /* 256MB */
#define FILE_PREFIX "Replay"

/* Windows port addition: the .gsr packet files hold raw binary packet data,
   so the file descriptors must be opened in BINARY mode. The MinGW CRT
   defaults to text mode, where read() stops at Ctrl-Z (0x1A) and write()
   translates '\n' to "\r\n" — both corrupt real packet data (docs/
   upstream-porting-notes.md §3l). */
#ifdef _WIN32
#define GSR_REPLAY_OPEN_BINARY O_BINARY
#else
#define GSR_REPLAY_OPEN_BINARY 0
#endif

static void gsr_replay_buffer_disk_set_impl_funcs(gsr_replay_buffer_disk *self);

static void gsr_av_packet_disk_init(gsr_av_packet_disk *self, const AVPacket *av_packet, size_t data_index, double timestamp) {
    self->packet = *av_packet;
    self->packet.data = NULL;
    self->data_index = data_index;
    self->timestamp = timestamp;
}

static gsr_replay_buffer_file* gsr_replay_buffer_file_create(char *replay_directory, size_t replay_storage_counter, double timestamp, int *replay_storage_fd) {
    gsr_replay_buffer_file *self = calloc(1, sizeof(gsr_replay_buffer_file));
    if(!self) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_av_packet_file_init: failed to create buffer file");
        return NULL;
    }

    if(create_directory_recursive(replay_directory) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_av_packet_file_init: failed to create replay directory: %s", replay_directory);
        free(self);
        return NULL;
    }

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s_%d.gsr", replay_directory, FILE_PREFIX, (int)replay_storage_counter);
    *replay_storage_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC | GSR_REPLAY_OPEN_BINARY, 0700);
    if(*replay_storage_fd <= 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_av_packet_file_init: failed to create replay file: %s", filename);
        free(self);
        return NULL;
    }

    self->id = replay_storage_counter;
    self->start_timestamp = timestamp;
    self->end_timestamp = timestamp;
    self->ref_counter = 1;
    self->fd = -1;

    self->packets = NULL;
    self->capacity_num_packets = 0;
    self->num_packets = 0;
    return self;
}

static gsr_replay_buffer_file* gsr_replay_buffer_file_ref(gsr_replay_buffer_file *self) {
    if(self->ref_counter >= 1)
        ++self->ref_counter;
    return self;
}

static void gsr_replay_buffer_file_free(gsr_replay_buffer_file *self, const char *replay_directory) {
    self->ref_counter = 0;

    if(self->fd > 0) {
        close(self->fd);
        self->fd = -1;
    }

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s_%d.gsr", replay_directory, FILE_PREFIX, (int)self->id);
    remove(filename);

    if(self->packets) {
        free(self->packets);
        self->packets = NULL;
    }
    self->num_packets = 0;
    self->capacity_num_packets = 0;

    free(self);
}

static void gsr_replay_buffer_file_unref(gsr_replay_buffer_file *self, const char *replay_directory) {
    if(self->ref_counter > 0)
        --self->ref_counter;

    if(self->ref_counter <= 0)
        gsr_replay_buffer_file_free(self, replay_directory);
}

static void gsr_replay_buffer_disk_clear(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;

    for(size_t i = 0; i < self->num_files; ++i) {
        gsr_replay_buffer_file_unref(self->files[i], self->replay_directory);
    }
    self->num_files = 0;

    if(self->storage_fd > 0) {
        close(self->storage_fd);
        self->storage_fd = 0;
    }

    self->storage_num_bytes_written = 0;
}

static void gsr_replay_buffer_disk_destroy(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    gsr_replay_buffer_disk_clear(replay_buffer);

    if(self->owns_directory) {
        remove(self->replay_directory);
        self->owns_directory = false;
    }
}

static bool file_write_all(int fd, const uint8_t *data, size_t size, size_t *bytes_written_total) {
    *bytes_written_total = 0;
    while(*bytes_written_total < size) {
        const ssize_t bytes_written = write(fd, data + *bytes_written_total, size - *bytes_written_total);
        if(bytes_written == -1) {
            if(errno == EAGAIN)
                continue;
            else
                return false;
        }
        *bytes_written_total += bytes_written;
    }
    return true;
}

static bool gsr_replay_buffer_disk_create_next_file(gsr_replay_buffer_disk *self, double timestamp) {
    if(self->num_files + 1 >= GSR_REPLAY_BUFFER_CAPACITY_NUM_FILES) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_buffer_disk_create_next_file: too many replay buffer files created! (> %d), either reduce the replay buffer time or report this as a bug", (int)GSR_REPLAY_BUFFER_CAPACITY_NUM_FILES);
        return false;
    }

    gsr_replay_buffer_file *replay_buffer_file = gsr_replay_buffer_file_create(self->replay_directory, self->storage_counter, timestamp, &self->storage_fd);
    if(!replay_buffer_file)
        return false;

    self->files[self->num_files] = replay_buffer_file;
    ++self->num_files;
    ++self->storage_counter;
    return true;
}

static bool gsr_replay_buffer_disk_append_to_current_file(gsr_replay_buffer_disk *self, const AVPacket *av_packet, double timestamp) {
    gsr_replay_buffer_file *replay_buffer_file = self->files[self->num_files - 1];
    replay_buffer_file->end_timestamp = timestamp;

    if(replay_buffer_file->num_packets + 1 >= replay_buffer_file->capacity_num_packets) {
        size_t new_capacity_num_packets = replay_buffer_file->capacity_num_packets * 2;
        if(new_capacity_num_packets == 0)
            new_capacity_num_packets = 256;

        void *new_packets = realloc(replay_buffer_file->packets, new_capacity_num_packets * sizeof(gsr_av_packet_disk));
        if(!new_packets) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_buffer_disk_append_to_current_file: failed to reallocate replay buffer file packets");
            return false;
        }

        replay_buffer_file->capacity_num_packets = new_capacity_num_packets;
        replay_buffer_file->packets = new_packets;
    }

    gsr_av_packet_disk *packet = &replay_buffer_file->packets[replay_buffer_file->num_packets];
    gsr_av_packet_disk_init(packet, av_packet, self->storage_num_bytes_written, timestamp);
    ++replay_buffer_file->num_packets;

    size_t bytes_written = 0;
    const bool file_written = file_write_all(self->storage_fd, av_packet->data, av_packet->size, &bytes_written);
    self->storage_num_bytes_written += bytes_written;
    if(self->storage_num_bytes_written >= REPLAY_BUFFER_FILE_SIZE_BYTES) {
        self->storage_num_bytes_written = 0;
        close(self->storage_fd);
        self->storage_fd = 0;
    }

    return file_written;
}

static void gsr_replay_buffer_disk_remove_first_file(gsr_replay_buffer_disk *self) {
    gsr_replay_buffer_file_unref(self->files[0], self->replay_directory);
    for(size_t i = 1; i < self->num_files; ++i) {
        self->files[i - 1] = self->files[i];
    }
    --self->num_files;
}

static bool gsr_replay_buffer_disk_append(gsr_replay_buffer *replay_buffer, const AVPacket *av_packet, double timestamp) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    bool success = false;

    if(self->storage_fd <= 0) {
        if(!gsr_replay_buffer_disk_create_next_file(self, timestamp))
            goto done;
    }

    const bool data_written = gsr_replay_buffer_disk_append_to_current_file(self, av_packet, timestamp);

    if(self->num_files > 1) {
        const double buffer_time_accumulated = timestamp - self->files[1]->start_timestamp;
        if(buffer_time_accumulated >= self->replay_buffer_time)
            gsr_replay_buffer_disk_remove_first_file(self);
    }

    success = data_written;

    done:
    return success;
}

static AVPacket* gsr_replay_buffer_disk_iterator_get_packet(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator iterator) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    assert(iterator.file_index < self->num_files);
    assert(iterator.packet_index < self->files[iterator.file_index]->num_packets);
    return &self->files[iterator.file_index]->packets[iterator.packet_index].packet;
}

static uint8_t* gsr_replay_buffer_disk_iterator_get_packet_data(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator iterator) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    assert(iterator.file_index < self->num_files);
    gsr_replay_buffer_file *file = self->files[iterator.file_index];
    assert(iterator.packet_index < file->num_packets);

    if(file->fd <= 0) {
        char filename[PATH_MAX];
        snprintf(filename, sizeof(filename), "%s/%s_%d.gsr", self->replay_directory, FILE_PREFIX, (int)file->id);
        file->fd = open(filename, O_RDONLY | GSR_REPLAY_OPEN_BINARY);
        if(file->fd <= 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_buffer_disk_iterator_get_packet_data: failed to open file");
            return NULL;
        }
    }

    const gsr_av_packet_disk *packet = &self->files[iterator.file_index]->packets[iterator.packet_index];
    if(lseek(file->fd, packet->data_index, SEEK_SET) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_buffer_disk_iterator_get_packet_data: failed to seek");
        return NULL;
    }

    uint8_t *packet_data = malloc(packet->packet.size);
    if(read(file->fd, packet_data, packet->packet.size) != packet->packet.size) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_buffer_disk_iterator_get_packet_data: failed to read data from file");
        free(packet_data);
        return NULL;
    }

    return packet_data;
}

static gsr_replay_buffer* gsr_replay_buffer_disk_clone(gsr_replay_buffer *replay_buffer) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    gsr_replay_buffer_disk *destination = calloc(1, sizeof(gsr_replay_buffer_disk));
    if(!destination)
        return NULL;

    gsr_replay_buffer_disk_set_impl_funcs(destination);

    destination->replay_buffer_time = self->replay_buffer_time;
    destination->storage_counter = self->storage_counter;
    destination->storage_num_bytes_written = self->storage_num_bytes_written;
    destination->storage_fd = 0; // We only want to read from the clone. If there is a need to write to it in the future then TODO change this

    for(size_t i = 0; i < self->num_files; ++i) {
        destination->files[i] = gsr_replay_buffer_file_ref(self->files[i]);
    }
    destination->num_files = self->num_files;

    snprintf(destination->replay_directory, sizeof(destination->replay_directory), "%s", self->replay_directory);
    destination->owns_directory = false;

    return (gsr_replay_buffer*)destination;
}

/* Binary search */
static size_t gsr_replay_buffer_file_find_packet_index_by_time_passed(const gsr_replay_buffer_file *self, int seconds) {
    const double now = clock_get_monotonic_seconds();
    if(self->num_packets == 0) {
        return 0;
    }

    size_t lower_bound = 0;
    size_t upper_bound = self->num_packets;
    size_t index = 0;

    for(;;) {
        index = lower_bound + (upper_bound - lower_bound) / 2;
        const gsr_av_packet_disk *packet = &self->packets[index];
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

    return index;
}

/* Binary search */
static gsr_replay_buffer_iterator gsr_replay_buffer_disk_find_file_index_by_time_passed(gsr_replay_buffer *replay_buffer, int seconds) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;

    const double now = clock_get_monotonic_seconds();
    if(self->num_files == 0) {
        return (gsr_replay_buffer_iterator){0, 0};
    }

    size_t lower_bound = 0;
    size_t upper_bound = self->num_files;
    size_t file_index = 0;

    for(;;) {
        file_index = lower_bound + (upper_bound - lower_bound) / 2;
        const gsr_replay_buffer_file *file = self->files[file_index];
        const double time_passed_since_file_start = now - file->start_timestamp;
        const double time_passed_since_file_end = now - file->end_timestamp;
        if(time_passed_since_file_start >= seconds && time_passed_since_file_end <= seconds) {
            break;
        } else if(time_passed_since_file_start >= seconds) {
            if(lower_bound == file_index)
                break;
            lower_bound = file_index;
        } else {
            if(upper_bound == file_index)
                break;
            upper_bound = file_index;
        }
    }

    const gsr_replay_buffer_file *file = self->files[file_index];
    const size_t packet_index = gsr_replay_buffer_file_find_packet_index_by_time_passed(file, seconds);

    return (gsr_replay_buffer_iterator){packet_index, file_index};
}

static gsr_replay_buffer_iterator gsr_replay_buffer_disk_find_keyframe(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator start_iterator, int stream_index, bool invert_stream_index) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    gsr_replay_buffer_iterator keyframe_iterator = {(size_t)-1, 0};
    size_t packet_index = start_iterator.packet_index;
    for(size_t file_index = start_iterator.file_index; file_index < self->num_files; ++file_index) {
        const gsr_replay_buffer_file *file = self->files[file_index];
        for(; packet_index < file->num_packets; ++packet_index) {
            const gsr_av_packet_disk *packet = &file->packets[packet_index];
            if((packet->packet.flags & AV_PKT_FLAG_KEY) && (invert_stream_index ? packet->packet.stream_index != stream_index : packet->packet.stream_index == stream_index)) {
                keyframe_iterator.packet_index = packet_index;
                keyframe_iterator.file_index = file_index;
                goto done;
            }
        }
        packet_index = 0;
    }
    done:
    return keyframe_iterator;
}

static bool gsr_replay_buffer_disk_iterator_next(gsr_replay_buffer *replay_buffer, gsr_replay_buffer_iterator *iterator) {
    gsr_replay_buffer_disk *self = (gsr_replay_buffer_disk*)replay_buffer;
    if(iterator->file_index >= self->num_files)
        return false;

    if(iterator->packet_index + 1 >= self->files[iterator->file_index]->num_packets) {
        if(iterator->file_index + 1 >= self->num_files)
            return false;

        if(self->files[iterator->file_index + 1]->num_packets == 0)
            return false;

        ++iterator->file_index;
        iterator->packet_index = 0;
        return true;
    } else {
        ++iterator->packet_index;
        return true;
    }
}

static void get_current_time(char *time_str, size_t time_str_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(time_str, time_str_size - 1, "%Y-%m-%d_%H-%M-%S", t);
}

static void gsr_replay_buffer_disk_set_impl_funcs(gsr_replay_buffer_disk *self) {
    self->replay_buffer.destroy = gsr_replay_buffer_disk_destroy;
    /* The files and the directory have to be removed even when the process exits */
    self->replay_buffer.destroy_at_exit = gsr_replay_buffer_disk_destroy;
    self->replay_buffer.append = gsr_replay_buffer_disk_append;
    self->replay_buffer.clear = gsr_replay_buffer_disk_clear;
    self->replay_buffer.iterator_get_packet = gsr_replay_buffer_disk_iterator_get_packet;
    self->replay_buffer.iterator_get_packet_data = gsr_replay_buffer_disk_iterator_get_packet_data;
    self->replay_buffer.clone = gsr_replay_buffer_disk_clone;
    self->replay_buffer.find_packet_index_by_time_passed = gsr_replay_buffer_disk_find_file_index_by_time_passed;
    self->replay_buffer.find_keyframe = gsr_replay_buffer_disk_find_keyframe;
    self->replay_buffer.iterator_next = gsr_replay_buffer_disk_iterator_next;
}

gsr_replay_buffer* gsr_replay_buffer_disk_create(const char *replay_directory, double replay_buffer_time) {
    assert(replay_buffer_time > 0);
    gsr_replay_buffer_disk *replay_buffer = calloc(1, sizeof(gsr_replay_buffer_disk));
    if(!replay_buffer)
        return NULL;

    char time_str[128];
    get_current_time(time_str, sizeof(time_str));

    replay_buffer->num_files = 0;
    replay_buffer->storage_counter = 0;
    replay_buffer->replay_buffer_time = replay_buffer_time;
    snprintf(replay_buffer->replay_directory, sizeof(replay_buffer->replay_directory), "%s/gsr-replay-%s.gsr", replay_directory, time_str);
    replay_buffer->owns_directory = true;

#ifdef _WIN32
    /* Windows port addition — crash-safe cleanup (docs/upstream-porting-notes.md §3l):
       the session directory above is uniquely timestamped and a clean exit removes
       it (destroy/destroy_at_exit). Any OTHER gsr-replay-*.gsr directory in the
       replay directory is therefore a leftover from a crashed previous session;
       sweep them so a crash does not leak .gsr files forever. The function is
       declared by the force-included gsr_win32_compat.h. */
    gsr_platform_replay_cleanup_stale_directories(replay_directory, replay_buffer->replay_directory + strlen(replay_directory) + 1);
#endif

    gsr_replay_buffer_disk_set_impl_funcs(replay_buffer);
    return (gsr_replay_buffer*)replay_buffer;
}
