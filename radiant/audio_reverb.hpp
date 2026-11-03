#pragma once
#include <stdint.h>
#include <stddef.h>

// Freeverb (Jezar at Dreampoint's public-domain design): eight parallel
// low-passed feedback combs and four series allpasses per channel, with the
// right channel's delays offset for stereo width. One medium-room preset; a
// document sub-mix runs one reverb fed by its voices' send levels
// (vibe/radiant/Radiant_Design_Speech.md §3.2).
struct AudioReverb;

// allocates on the calling (UI) thread; null when out of memory.
AudioReverb* audio_reverb_create(uint32_t sample_rate);
size_t audio_reverb_storage_bytes(uint32_t sample_rate);
bool audio_reverb_active(const AudioReverb* reverb);
void audio_reverb_reset(AudioReverb* reverb);
void audio_reverb_destroy(AudioReverb* reverb);
// adds the wet response to stereo `input` (`frames` frames) into stereo
// `output`. Real-time safe: no allocation, locks or system calls.
void audio_reverb_process(AudioReverb* reverb, const float* input, float* output, uint32_t frames);
