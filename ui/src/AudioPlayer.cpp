#include "../include/AudioPlayer.hpp"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSIZE 4096

namespace gsr {
    AudioPlayer::~AudioPlayer() {
        if(thread.joinable()) {
            stop_playing_audio = true;
            thread.join();
        }
    }

    bool AudioPlayer::play(const char *filepath) {
        if(thread.joinable()) {
            stop_playing_audio = true;
            thread.join();
        }

        stop_playing_audio = false;

        FILE *file = fopen(filepath, "rb");
        if(!file) {
            fprintf(stderr, "gsr ui: error: AudioPlayer::play: failed to open %s\n", filepath);
            return false;
        }

        thread = std::thread([this, file]() {
            /* Raw s16le 48kHz stereo PCM (see the header's conversion hint). */
            WAVEFORMATEX format;
            memset(&format, 0, sizeof(format));
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = 2;
            format.nSamplesPerSec = 48000;
            format.wBitsPerSample = 16;
            format.nBlockAlign = (format.nChannels * format.wBitsPerSample) / 8;
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

            HWAVEOUT wave_out = NULL;
            if(waveOutOpen(&wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
                fprintf(stderr, "gsr ui: error: AudioPlayer::play: waveOutOpen failed\n");
                fclose(file);
                return;
            }

            WAVEHDR header;
            memset(&header, 0, sizeof(header));
            header.lpData = (LPSTR)malloc(BUFSIZE);
            header.dwBufferLength = 0;
            header.dwFlags = WHDR_DONE;

            for(;;) {
                if(stop_playing_audio)
                    break;

                const size_t bytes_read = fread(header.lpData, 1, BUFSIZE, file);
                if(bytes_read == 0)
                    break;

                header.dwBufferLength = (DWORD)bytes_read;
                header.dwFlags = 0;
                waveOutPrepareHeader(wave_out, &header, sizeof(header));
                if(waveOutWrite(wave_out, &header, sizeof(header)) != MMSYSERR_NOERROR)
                    break;

                /* Wait for this chunk to finish playing before reusing the buffer. */
                while(!stop_playing_audio && !(header.dwFlags & WHDR_DONE))
                    Sleep(5);

                waveOutUnprepareHeader(wave_out, &header, sizeof(header));
            }

            waveOutReset(wave_out);
            free(header.lpData);
            waveOutClose(wave_out);
            fclose(file);
        });

        return true;
    }
}
#else
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
#endif
