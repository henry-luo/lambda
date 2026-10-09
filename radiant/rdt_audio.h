#pragma once
#include <stddef.h>
#include <stdbool.h>

// Decoded native audio borrows source bytes only during open (D7.1.2v2).
typedef struct RdtAudio RdtAudio;
typedef enum { RDT_AUDIO_READY, RDT_AUDIO_PLAYING, RDT_AUDIO_PAUSED,
    RDT_AUDIO_ENDED, RDT_AUDIO_ERROR } RdtAudioState;
#ifdef __cplusplus
extern "C" {
#endif
RdtAudio* rdt_audio_open_bytes(const void* bytes, size_t length);
void rdt_audio_destroy(RdtAudio* audio);
bool rdt_audio_play(RdtAudio* audio, float volume);
void rdt_audio_pause(RdtAudio* audio);
RdtAudioState rdt_audio_state(RdtAudio* audio);
#ifdef __cplusplus
}
#endif
