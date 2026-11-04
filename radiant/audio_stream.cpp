#include "audio_miniaudio.hpp"
#include "audio_stream.hpp"
#include "../lib/mem.h"
#include <math.h>

struct AudioStream {
    ma_pcm_rb ring;
    float* samples;
    unsigned capacity;
    uint64_t frame, written, underrun_frames;
    float gain;
    int state, finished;
};
static AudioStreamState stream_state(AudioStream* stream) {
    return (AudioStreamState)__atomic_load_n(&stream->state, __ATOMIC_ACQUIRE);
}
size_t audio_stream_storage_bytes(unsigned capacity) {
    return capacity && capacity <= AUDIO_STREAM_CAPACITY ? sizeof(AudioStream) + (size_t)capacity * 2 * sizeof(float) : 0;
}
AudioStream* audio_stream_create(unsigned capacity) {
    if (!capacity || capacity > AUDIO_STREAM_CAPACITY) return nullptr;
    auto* stream = (AudioStream*)mem_calloc(1, sizeof(AudioStream), MEM_CAT_RENDER);
    if (!stream) return nullptr;
    stream->samples = (float*)mem_alloc((size_t)capacity * 2 * sizeof(float), MEM_CAT_RENDER);
    if (!stream->samples || ma_pcm_rb_init(ma_format_f32, 2, capacity, stream->samples, nullptr, &stream->ring) != MA_SUCCESS) {
        mem_free(stream->samples); mem_free(stream); return nullptr;
    }
    stream->capacity = capacity; stream->gain = 1; return stream;
}
void audio_stream_destroy(AudioStream* stream) {
    if (!stream) return;
    ma_pcm_rb_uninit(&stream->ring); mem_free(stream->samples); mem_free(stream);
}
void audio_stream_reset(AudioStream* stream, uint64_t frame) {
    if (!stream) return;
    ma_pcm_rb_reset(&stream->ring);
    stream->frame = stream->written = frame; stream->underrun_frames = 0;
    stream->finished = 0; stream->state = AUDIO_STREAM_READY;
}
void audio_stream_fail(AudioStream* stream) {
    if (stream) __atomic_store_n(&stream->state, AUDIO_STREAM_ERROR, __ATOMIC_RELEASE);
}
static unsigned stream_append(AudioStream* stream, const float* samples, unsigned frames) {
    unsigned done = 0;
    while (done < frames) {
        ma_uint32 count = frames - done; void* target = nullptr;
        if (ma_pcm_rb_acquire_write(&stream->ring, &count, &target) != MA_SUCCESS || !count) break;
        if (samples) memcpy(target, samples + (size_t)done * 2, (size_t)count * 2 * sizeof(float));
        else memset(target, 0, (size_t)count * 2 * sizeof(float));
        ma_pcm_rb_commit_write(&stream->ring, count); done += count;
    }
    __atomic_fetch_add(&stream->written, done, __ATOMIC_RELEASE); return done;
}
unsigned audio_stream_write(AudioStream* stream, uint64_t frame, const float* samples, unsigned frames) {
    if (!stream || stream_state(stream) == AUDIO_STREAM_ERROR || __atomic_load_n(&stream->finished, __ATOMIC_ACQUIRE)) return 0;
    if (frames > AUDIO_STREAM_PACKET || frames > UINT64_MAX - frame) { audio_stream_fail(stream); return 0; }
    // validate before publication so a malformed packet cannot enqueue a valid prefix.
    if (samples) for (size_t i = 0; i < (size_t)frames * 2; i++) if (!isfinite(samples[i])) { audio_stream_fail(stream); return 0; }
    uint64_t written = __atomic_load_n(&stream->written, __ATOMIC_ACQUIRE);
    if (frame > written) {
        uint64_t gap = frame - written;
        unsigned count = gap < stream->capacity ? (unsigned)gap : stream->capacity;
        if (stream_append(stream, nullptr, count) != gap) return 0;
        written = frame;
    }
    unsigned skip = written - frame < frames ? (unsigned)(written - frame) : frames;
    return skip + stream_append(stream, samples ? samples + (size_t)skip * 2 : nullptr, frames - skip);
}
void audio_stream_finish(AudioStream* stream) {
    if (stream) __atomic_store_n(&stream->finished, 1, __ATOMIC_RELEASE);
}
bool audio_stream_play(AudioStream* stream) {
    if (!stream || stream_state(stream) == AUDIO_STREAM_ERROR || stream_state(stream) == AUDIO_STREAM_ENDED) return false;
    __atomic_store_n(&stream->state, AUDIO_STREAM_PLAYING, __ATOMIC_RELEASE); return true;
}
void audio_stream_pause(AudioStream* stream) {
    if (stream && stream_state(stream) == AUDIO_STREAM_PLAYING) __atomic_store_n(&stream->state, AUDIO_STREAM_PAUSED, __ATOMIC_RELEASE);
}
bool audio_stream_gain(AudioStream* stream, float gain) {
    if (!stream || !isfinite(gain) || gain < 0 || gain > 1) return false;
    __atomic_store(&stream->gain, &gain, __ATOMIC_RELEASE); return true;
}
AudioStreamSnapshot audio_stream_snapshot(AudioStream* stream) {
    if (!stream) return {AUDIO_STREAM_ERROR};
    float gain; __atomic_load(&stream->gain, &gain, __ATOMIC_ACQUIRE);
    return {stream_state(stream), __atomic_load_n(&stream->frame, __ATOMIC_ACQUIRE),
        __atomic_load_n(&stream->written, __ATOMIC_ACQUIRE), __atomic_load_n(&stream->underrun_frames, __ATOMIC_ACQUIRE),
        ma_pcm_rb_available_read(&stream->ring), stream->capacity, gain,
        __atomic_load_n(&stream->finished, __ATOMIC_ACQUIRE) != 0};
}
void audio_stream_mix(void* source, float* output, uint32_t frames, uint64_t) {
    auto* stream = (AudioStream*)source;
    if (!stream || !output || stream_state(stream) != AUDIO_STREAM_PLAYING) return;
    unsigned done = 0; float gain; __atomic_load(&stream->gain, &gain, __ATOMIC_ACQUIRE);
    while (done < frames) {
        ma_uint32 count = frames - done; void* samples = nullptr;
        if (ma_pcm_rb_acquire_read(&stream->ring, &count, &samples) != MA_SUCCESS || !count) break;
        for (unsigned i = 0; i < count * 2; i++) output[(size_t)done * 2 + i] += ((float*)samples)[i] * gain;
        ma_pcm_rb_commit_read(&stream->ring, count); done += count;
    }
    // starvation holds media time; video must not run ahead while its producer is stalled.
    __atomic_fetch_add(&stream->frame, done, __ATOMIC_RELEASE);
    bool finished = __atomic_load_n(&stream->finished, __ATOMIC_ACQUIRE) != 0;
    if (finished && !ma_pcm_rb_available_read(&stream->ring)) {
        // a producer fault must not be overwritten by normal end-of-stream delivery.
        int playing = AUDIO_STREAM_PLAYING;
        __atomic_compare_exchange_n(&stream->state, &playing, AUDIO_STREAM_ENDED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
    else __atomic_fetch_add(&stream->underrun_frames, frames - done, __ATOMIC_RELEASE);
}
