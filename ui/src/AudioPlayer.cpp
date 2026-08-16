#include "../include/AudioPlayer.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#define BUFSIZE 4096

namespace gsr {
    AudioPlayer::~AudioPlayer() {
        if(thread.joinable()) {
            stop_playing_audio = true;
            thread.join();
        }

        if(audio_file_fd > 0)
            close(audio_file_fd);
    }

    bool AudioPlayer::play(const char *filepath) {
        if(thread.joinable()) {
            stop_playing_audio = true;
            thread.join();
        }

        stop_playing_audio = false;
        audio_file_fd = open(filepath, O_RDONLY);
        if(audio_file_fd == -1)
            return false;

        thread = std::thread([this]() {
            pa_sample_spec ss;
            ss.format = PA_SAMPLE_S16LE;
            ss.rate = 48000;
            ss.channels = 2;

            pa_simple *s = NULL;
            int error;

            /* Create a new playback stream */
            if(!(s = pa_simple_new(NULL, "gsr-ui-audio-playback", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) {
                fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
                goto finish;
            }

            uint8_t buf[BUFSIZE];
            for(;;) {
                ssize_t r;

                if(stop_playing_audio)
                    goto finish;

                if((r = read(audio_file_fd, buf, sizeof(buf))) <= 0) {
                    if(r == 0) /* EOF */
                        break;

                    fprintf(stderr, __FILE__": read() failed: %s\n", strerror(errno));
                    goto finish;
                }

                if(pa_simple_write(s, buf, (size_t) r, &error) < 0) {
                    fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
                    goto finish;
                }
            }

            if(pa_simple_drain(s, &error) < 0) {
                fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
                goto finish;
            }

            finish:
            if(s)
                pa_simple_free(s);

            close(audio_file_fd);
            audio_file_fd = -1;
        });

        return true;
    }
}