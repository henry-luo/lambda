#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// decoded native audio borrows bytes only during open and owns device-free stereo PCM.
// the UI owner excludes its bus voice before play/pause/fail/destroy; only state is atomic.
#define RDT_AUDIO_SOURCE_LIMIT (256u * 1024u * 1024u)
#define RDT_AUDIO_PCM_LIMIT (256u * 1024u * 1024u)
typedef struct RdtAudio RdtAudio;
typedef enum { RDT_AUDIO_READY, RDT_AUDIO_PLAYING, RDT_AUDIO_PAUSED,
    RDT_AUDIO_ENDED, RDT_AUDIO_ERROR } RdtAudioState;
typedef struct {
    float left, right, pitch, smoothing, lowpass;
    bool mono, loop;
} RdtAudioParameters;
typedef struct {
    const void* owner;
    size_t shared_bytes, player_bytes;
} RdtAudioStorage;
typedef struct {
    RdtAudioState state;
    double position, duration, left, right, pitch, gain, peak, rms;
    uint64_t rendered;
    bool loop;
    uint64_t identity;
    bool armed;
} RdtAudioInfo;
#ifdef __cplusplus
extern "C" {
#endif
RdtAudio* rdt_audio_open_bytes(const void* bytes, size_t length);
// storage_budget includes the player and retained PCM; document owners share a finite budget.
RdtAudio* rdt_audio_open_bytes_limit(const void* bytes, size_t length, size_t storage_budget);
size_t rdt_audio_storage_bytes(const RdtAudio* audio);
size_t rdt_audio_frames(const RdtAudio* audio);
// clones share immutable PCM, but never a cursor, filter, gain or meter state.
RdtAudio* rdt_audio_clone(const RdtAudio* source);
RdtAudioStorage rdt_audio_storage(const RdtAudio* audio);
RdtAudioParameters rdt_audio_default_parameters(void);
bool rdt_audio_parameters(RdtAudio* audio, const RdtAudioParameters* parameters);
bool rdt_audio_gain(RdtAudio* audio, float gain);
void rdt_audio_identity(RdtAudio* audio, uint64_t identity);
// sample-driver primitives transfer an already owned PCM reader; they never allocate or free.
void rdt_audio_replace(RdtAudio* audio, RdtAudio* prepared);
void rdt_audio_stop(RdtAudio* audio);
// caller excludes the bus reader; the displaced player's sample owner retires afterwards.
void rdt_audio_swap(RdtAudio* destination, RdtAudio* candidate);
bool rdt_audio_info(const RdtAudio* audio, RdtAudioInfo* info);
void rdt_audio_destroy(RdtAudio* audio);
bool rdt_audio_play(RdtAudio* audio, float volume);
void rdt_audio_pause(RdtAudio* audio);
RdtAudioState rdt_audio_state(RdtAudio* audio);
const char* rdt_audio_state_name(RdtAudioState state);
void rdt_audio_fail(RdtAudio* audio);
// audio-bus callback: adds stereo samples without allocation, resource access or DOM/GC work.
void rdt_audio_mix(void* source, float* output, uint32_t frames, uint64_t clock);
#ifdef __cplusplus
}
#endif
