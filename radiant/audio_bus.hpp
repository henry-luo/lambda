#pragma once
#include <stddef.h>
#include <stdint.h>

// Window audio bus and document sub-mixes (vibe/radiant/Radiant_Design_Speech.md §3.2).
// The bus owns the one output device and mixes every live sub-mix; each
// document owns one sub-mix, whose clock counts the frames it has rendered.
// Streams ("voices": MIDI sessions, speech) render into a sub-mix. Handles are
// generation-checked, so a handle outliving its sub-mix or voice is inert.
// Every function here is for the UI thread, except the render callbacks.

#define AUDIO_BUS_RATE 48000u
typedef uint64_t AudioSubmixId;
typedef uint64_t AudioVoiceId;

// adds `frames` stereo frames to `out`, the first at sub-mix frame `clock`. It
// runs on the audio thread, or on the UI thread when the bus is offline, and
// never allocates, blocks or touches the DOM.
typedef void (*AudioVoiceRender)(void* source, float* out, uint32_t frames, uint64_t clock);

AudioSubmixId audio_submix_create(void);
// removes the sub-mix's voices; their sources may be freed afterwards.
void audio_submix_destroy(AudioSubmixId submix);
uint64_t audio_submix_clock(AudioSubmixId submix);
bool audio_submix_has_voices(AudioSubmixId submix);

// adds a voice, opening the bus device unless the bus is offline. Returns 0 and
// sets `error` when the device is unavailable or the tables are full.
AudioVoiceId audio_voice_add(AudioSubmixId submix, AudioVoiceRender render, void* source, const char** error);
// after removal the audio thread never touches `source` again.
void audio_voice_remove(AudioVoiceId voice);
// excludes the audio thread from the voice while the UI thread changes its
// source; the voice is silent for any buffer rendered meanwhile.
bool audio_voice_lock(AudioVoiceId voice);
void audio_voice_unlock(AudioVoiceId voice);
// the voice's reverb send, 0 (dry) to 1; the sub-mix gets its reverb on the first
// send above 0. Voices start dry.
bool audio_voice_set_send(AudioVoiceId voice, float level);
float audio_voice_send(AudioVoiceId voice);
// the output device was lost after the voice was added; it stays silent.
bool audio_voice_lost(AudioVoiceId voice);

// handles a lost device: marks its voices lost and closes it, so the next
// voice reopens one. Called from the frame tick.
void audio_bus_poll(void);

// Offline output (LAMBDA_AUDIO_OUTPUT=offline, or set by tests): no device is
// opened, and each sub-mix advances only when its owner renders it.
bool audio_bus_offline(void);
void audio_bus_set_offline(bool offline);
// renders `frames` of one sub-mix on the calling thread and advances its clock;
// the mix goes to `capture` (stereo) when given. Offline only.
bool audio_submix_render(AudioSubmixId submix, float* capture, uint32_t frames);
