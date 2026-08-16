#pragma once

#include <thread>

namespace gsr {
    // Only plays raw stereo PCM audio in 48000hz in s16le format.
    // Use this command to convert an audio file (input.wav) to a format playable by this class (output.pcm):
    // ffmpeg -i input.wav -f s16le -acodec pcm_s16le -ar 48000 output.pcm
    class AudioPlayer {
    public:
        AudioPlayer() = default;
        ~AudioPlayer();
        AudioPlayer(const AudioPlayer&) = delete;
        AudioPlayer& operator=(const AudioPlayer&) = delete;

        bool play(const char *filepath);
    private:
        std::thread thread;
        bool stop_playing_audio = false;
        int audio_file_fd = -1;
    };
}