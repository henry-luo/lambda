#pragma once
#include <stddef.h>
#include <stdint.h>
#include "audio_graph.hpp"

// Window audio bus and document sub-mixes (vibe/radiant/Radiant_Design_Speech.md §3.2).
// The bus owns the one output device and mixes every live sub-mix; each
// document owns one sub-mix, whose clock counts the frames it has rendered.
// Streams ("voices": MIDI sessions, speech) render into a sub-mix. Handles are
// generation-checked, so a handle outliving its sub-mix or voice is inert.
// Every function here is for the UI thread, except the render callbacks.

#define AUDIO_BUS_RATE 48000u
typedef uint64_t AudioSubmixId;
typedef uint64_t AudioVoiceId;
typedef uint64_t AudioRouteRef;
static constexpr unsigned AUDIO_ROUTE_REFERENCES = 128;
enum AudioVoiceLoss { AUDIO_VOICE_OK, AUDIO_VOICE_DEVICE_LOST, AUDIO_VOICE_ROUTE_LOST };
// shared opaque native identities never wrap; provider rows may impose their signed-token bound.
uint64_t audio_bus_identity(uint64_t maximum = UINT64_MAX - 1);

// adds `frames` stereo frames to `out`, the first at sub-mix frame `clock`. It
// runs on the audio thread, or on the UI thread when the bus is offline, and
// never allocates, blocks or touches the DOM.
typedef void (*AudioVoiceRender)(void* source, float* out, uint32_t frames, uint64_t clock);
// native sample drivers run under submix exclusion; no allocation, blocking, DOM/GC or script.
struct AudioSampleDriver {
    void* owner;
    void (*apply)(void* owner, uint64_t clock);
    uint64_t (*next)(void* owner);
    void (*after)(void* owner, uint64_t clock);
    void (*lost)(void* owner, uint64_t clock);
};
bool audio_submix_lock(AudioSubmixId submix);
void audio_submix_unlock(AudioSubmixId submix);
bool audio_submix_prepare_output(AudioSubmixId submix, bool required, const char** error);
bool audio_submix_bind_driver(AudioSubmixId submix, const AudioSampleDriver& driver);
void audio_submix_unbind_driver(AudioSubmixId submix, void* owner);
// caller holds submix exclusion; updates the clock-work flag without opening a device.
void audio_submix_refresh_work_locked(AudioSubmixId submix);

// virtual submixes are explicit render owners, never device callback inputs.
AudioSubmixId audio_submix_create(bool virtual_output = false);
// removes the sub-mix's voices; their sources may be freed afterwards.
void audio_submix_destroy(AudioSubmixId submix);
uint64_t audio_submix_clock(AudioSubmixId submix);
bool audio_submix_has_voices(AudioSubmixId submix);
// includes native DSP tails after the last source voice has retired.
bool audio_submix_has_work(AudioSubmixId submix);
struct AudioRouteChange { AudioVoiceId voice; AudioRouteId route; };
AudioGraphId audio_submix_graph_open(AudioSubmixId submix);
void audio_submix_graph_close(AudioSubmixId submix, AudioGraphId graph);
void audio_submix_graph_reset(AudioSubmixId submix, AudioGraphId graph);
bool audio_submix_graph_prepare(AudioSubmixId submix, AudioGraphId graph, const AudioGraphConfig& config,
    AudioGraph** prepared, const char** error);
AudioRouteId audio_submix_graph_route(AudioSubmixId submix, AudioGraphId graph, const AudioGraph* prepared, const char* name);
bool audio_submix_graph_has_route(AudioSubmixId submix, AudioGraphId graph, AudioRouteId route);
bool audio_submix_graph_commit(AudioSubmixId submix, AudioGraphId graph, AudioGraph* prepared,
    const AudioRouteChange* changes, unsigned count, const char** error = nullptr);
bool audio_submix_graph_snapshot(AudioSubmixId submix, AudioGraphId graph, AudioGraphSnapshot* snapshot);
bool audio_submix_route_valid(AudioSubmixId submix, AudioRouteId route);
// retained providers pin a named route even while paused or awaiting prepared samples.
// a route/voice update publishes atomically; zero route releases the pin and restores default output.
bool audio_submix_bind_route(AudioSubmixId submix, AudioRouteRef* reference, AudioVoiceId voice, AudioRouteId route);
bool audio_submix_route_ref_valid(AudioSubmixId submix, AudioRouteRef reference);
void audio_submix_release_route(AudioSubmixId submix, AudioRouteRef reference);

// adds a voice, opening the bus device unless the bus is offline. Returns 0 and
// sets `error` when the device is unavailable or the tables are full.
AudioVoiceId audio_voice_add(AudioSubmixId submix, AudioVoiceRender render, void* source, const char** error, AudioRouteId route = 0, bool active = true);
// native sample-driver operations; the submix is already excluded by its caller.
bool audio_voice_set_active_locked(AudioVoiceId voice, bool active);
void audio_voice_remove_locked(AudioVoiceId voice);
bool audio_voice_try_lock(AudioVoiceId voice);
// after removal the audio thread never touches `source` again.
void audio_voice_remove(AudioVoiceId voice);
// excludes the audio thread from the voice while the UI thread changes its
// source; the voice is silent for any buffer rendered meanwhile.
bool audio_voice_lock(AudioVoiceId voice);
bool audio_voice_lock_route(AudioVoiceId voice, AudioRouteId route);
void audio_voice_unlock(AudioVoiceId voice);
// the voice's reverb send, 0 (dry) to 1; the sub-mix gets its reverb on the first
// send above 0. Voices start dry.
bool audio_voice_set_send(AudioVoiceId voice, float level);
float audio_voice_send(AudioVoiceId voice);
// the output device was lost after the voice was added; it stays silent.
bool audio_voice_lost(AudioVoiceId voice);
AudioVoiceLoss audio_voice_loss(AudioVoiceId voice);

// handles a lost device: marks its voices lost and closes it, so the next
// voice reopens one. Called from the frame tick.
void audio_bus_poll(void);
// UI/host device reconfiguration retires callback readers and marks existing voices lost.
void audio_bus_retire_output(void);

// Offline output (LAMBDA_AUDIO_OUTPUT=offline, or set by tests): no device is
// opened, and each sub-mix advances only when its owner renders it.
bool audio_bus_offline(void);
void audio_bus_set_offline(bool offline);
// the actual output callback kernel; offline native controls may exercise it with the device excluded.
void audio_bus_mix(float* output, uint32_t frames);
// renders `frames` of one sub-mix on the calling thread and advances its clock;
// the mix goes to `capture` (stereo) when given. Offline only.
bool audio_submix_render(AudioSubmixId submix, float* capture, uint32_t frames);
