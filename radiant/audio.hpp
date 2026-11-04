#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rdt_audio.h"
#include "midi_player.hpp"
#include "audio_bus.hpp"
struct DomDocument;
struct DomNode;
struct DomElement;
enum AudioPlayerNotice { AUDIO_PLAYER_PENDING, AUDIO_PLAYER_MANUAL, AUDIO_PLAYER_CLOSE, AUDIO_PLAYER_LOST };
typedef bool (*AudioPlayerObserver)(void* owner, uint64_t token, AudioPlayerNotice notice);
struct AudioPlayerLease { uint64_t token; RdtAudio* player; AudioVoiceId voice; };
// native cue preparation owns ordinary accounted document players; no pointer crosses the host waist.
uint64_t radiant_audio_clone_ready(DomDocument* document, uint64_t asset, const RdtAudioParameters* parameters, float gain);
bool radiant_audio_identity(DomDocument* document, uint64_t token, uint64_t identity);
// caller holds the document submix; parked readers do not acquire an output device.
bool radiant_audio_bind_lease_locked(DomDocument* document, uint64_t token, AudioSubmixId submix,
    void* owner, AudioPlayerObserver observer, bool reserve_voice, AudioRouteId route, AudioPlayerLease* lease,
    bool* new_voice = nullptr, bool* new_binding = nullptr);
void radiant_audio_unbind_lease_locked(DomDocument* document, uint64_t token, void* owner, bool remove_voice);
uint64_t radiant_audio_open(DomDocument* document, const void* bytes, size_t length);
// output-free preparation registers the quota owner before borrowers; adoption always consumes the player.
size_t radiant_audio_available_pcm(DomDocument* document);
bool radiant_audio_prepare_document(DomDocument* document);
uint64_t radiant_audio_adopt(DomDocument* document, RdtAudio* player);
// scene sources retain independent players over an existing immutable sample owner.
// reuse replaces a prepared player at a buffer boundary; rejection preserves the old player.
uint64_t radiant_audio_source_start(DomDocument* document, uint64_t asset, uint64_t reuse,
    const RdtAudioParameters* parameters, float gain, AudioRouteId route = 0);
struct AudioPlayerRoute { uint64_t token; AudioRouteId route; };
// staged graph and live/paused source routes publish together; failed admission keeps ownership with the caller.
bool radiant_audio_graph_may_edit(DomDocument* document, AudioGraphId graph);
// dry providers still express audible intent when a muted bus has no DSP history (D11.3.1).
bool radiant_audio_graph_input_audible(AudioSubmixId submix, AudioGraphId graph, AudioRouteId route, bool active, double gain);
bool radiant_audio_graph_commit(DomDocument* document, AudioGraphId graph, AudioGraph* prepared,
    const AudioPlayerRoute* routes, unsigned count, const char** error = nullptr);
bool radiant_audio_parameters(DomDocument* document, uint64_t token, const RdtAudioParameters* parameters, float gain = -1);
bool radiant_audio_info(DomDocument* document, uint64_t token, RdtAudioInfo* info);
// copied native accounting includes each immutable PCM owner once across all players.
bool radiant_audio_usage(DomDocument* document, size_t* players, size_t* bytes);
// native streaming providers share the document's PCM/player budget; reservation IDs never alias.
uint64_t radiant_audio_storage_acquire(DomDocument* document, size_t bytes);
void radiant_audio_storage_release(DomDocument* document, uint64_t reservation);
// native main-thread sampling only; these hooks never run on the audio callback.
typedef void (*AudioOwnerPoll)(void* owner, double now_ms);
bool radiant_audio_bind_owner(DomDocument* document, void* owner, AudioOwnerPoll poll);
void radiant_audio_unbind_owner(DomDocument* document, void* owner);
bool radiant_audio_play(DomDocument* document, uint64_t token, float volume);
bool radiant_audio_pause(DomDocument* document, uint64_t token);
bool radiant_audio_close(DomDocument* document, uint64_t token);
const char* radiant_audio_state(DomDocument* document, uint64_t token);
uint64_t radiant_midi_open(DomElement* owner, const MidiEvent* events, size_t count,
    double duration, const void* font, size_t font_size);
bool radiant_midi_command(DomDocument* document, uint64_t token, const char* command, double value);
bool radiant_midi_info(DomDocument* document, uint64_t token, MidiSnapshot* snapshot);
struct AudioProviderInfo {
    AudioRouteId route;
    AudioVoiceLoss loss;
    const char* state;
    double position, gain;
    bool route_valid;
};
struct AudioProviderOps {
    const char* name;
    bool (*route)(DomDocument*, uint64_t, AudioRouteId, AudioRouteId, const char**);
    bool (*read)(DomDocument*, uint64_t, AudioProviderInfo*);
    RdtAudio* (*snapshot)(DomDocument*, uint64_t, double, uint32_t);
};
const AudioProviderOps* radiant_audio_provider(const char* name);
uint64_t radiant_audio_provider_snapshot(DomDocument* document, const AudioProviderOps* provider,
    uint64_t token, double position, uint32_t frames, const char** error);
RdtAudio* radiant_midi_snapshot(DomDocument* document, uint64_t token, double position, uint32_t frames);
RdtAudio* radiant_speech_snapshot(DomDocument* document, uint64_t token, double position, uint32_t frames);
RdtAudio* radiant_media_snapshot(DomDocument* document, uint64_t token, double position, uint32_t frames);
bool radiant_midi_route(DomDocument* document, uint64_t token, AudioRouteId expected, AudioRouteId route, const char** error);
bool radiant_midi_routing(DomDocument* document, uint64_t token, AudioProviderInfo* info);
bool radiant_media_route(DomDocument* document, uint64_t token, AudioRouteId expected, AudioRouteId route, const char** error);
bool radiant_media_routing(DomDocument* document, uint64_t token, AudioProviderInfo* info);
bool radiant_media_graph_audible(DomDocument* document, AudioGraphId graph);
void radiant_audio_prune(DomDocument* document);
void radiant_audio_follow_rebuild(DomDocument* document, DomNode* old_root, DomNode* new_root);
// the document's sub-mix on the window bus, created on first use (§3.2).
AudioSubmixId radiant_document_submix(DomDocument* document);
// seconds on the document's sub-mix clock; 0 before its first audible stream.
double radiant_audio_clock(DomDocument* document);
uint64_t radiant_audio_frame(DomDocument* document);
// frame-clock tick: retires a lost device and, with offline output, advances the
// sub-mix to `now_ms`. Returns whether the sub-mix needs further frames.
bool radiant_audio_tick(DomDocument* document, double now_ms);
// voices/tails keep owner polling alive; offline output also consumes those frame intervals.
bool radiant_audio_frames_pending(DomDocument* document);
// D11.3.1 audible-output gate. A local document may start audio; a network
// document needs sticky activation from trusted host input; only the host
// grants activation up front (`view --autoplay`).
void radiant_audio_grant_activation(bool granted);
void radiant_audio_note_activation(DomDocument* document);
bool radiant_document_may_start_audio(DomDocument* document);
