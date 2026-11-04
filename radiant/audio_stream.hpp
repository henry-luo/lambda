#pragma once
#include "audio_bus.hpp"

// D5.5.2: one native producer publishes owned stereo PCM to the existing bus callback.
static constexpr unsigned AUDIO_STREAM_CAPACITY = 2 * AUDIO_BUS_RATE;
static constexpr unsigned AUDIO_STREAM_PACKET = 65536;
enum AudioStreamState { AUDIO_STREAM_READY, AUDIO_STREAM_PLAYING, AUDIO_STREAM_PAUSED,
    AUDIO_STREAM_ENDED, AUDIO_STREAM_ERROR };
struct AudioStream;
struct AudioStreamSnapshot {
    AudioStreamState state;
    uint64_t frame, written, underrun_frames;
    unsigned queued, capacity;
    float gain;
    bool finished;
};
AudioStream* audio_stream_create(unsigned capacity = AUDIO_STREAM_CAPACITY);
size_t audio_stream_storage_bytes(unsigned capacity = AUDIO_STREAM_CAPACITY);
// the owner excludes both producer and bus reader before reset/destroy.
void audio_stream_destroy(AudioStream* stream);
void audio_stream_reset(AudioStream* stream, uint64_t frame);
// producer-only: returns consumed source frames; null samples describe silence.
// PTS gaps queue silence; overlaps consume only the not-yet-published suffix.
unsigned audio_stream_write(AudioStream* stream, uint64_t frame, const float* samples, unsigned frames);
void audio_stream_finish(AudioStream* stream);
// transport controls exclude the bus reader; gain may change during rendering.
bool audio_stream_play(AudioStream* stream);
void audio_stream_pause(AudioStream* stream);
bool audio_stream_gain(AudioStream* stream, float gain);
void audio_stream_fail(AudioStream* stream);
AudioStreamSnapshot audio_stream_snapshot(AudioStream* stream);
// an AudioVoiceRender: no decoding, allocation, waiting, DOM or GC access.
void audio_stream_mix(void* stream, float* output, uint32_t frames, uint64_t clock);
