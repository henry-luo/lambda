#pragma once
#include "audio_cues.hpp"

struct AudioCueOwner;
typedef void (*AudioCueOwnerNotice)(void* owner, uint64_t token, AudioPlayerNotice notice);
AudioCueOwner* audio_cue_owner_create(DomDocument* document, AudioSubmixId submix, uint64_t epoch,
    void* observer = nullptr, AudioCueOwnerNotice notice = nullptr);
AudioCueQueue* audio_cue_owner_queue(AudioCueOwner* owner);
// owned readers close on retirement/destruction; scene source transports stay with their existing owner.
bool audio_cue_owner_bind_locked(AudioCueOwner* owner, uint64_t token, bool owned, bool voice,
    AudioRouteId route, AudioPlayerLease* lease, bool* new_voice = nullptr, bool* new_binding = nullptr);
void audio_cue_owner_unbind_locked(AudioCueOwner* owner, uint64_t token, bool remove_voice);
void audio_cue_owner_collect(AudioCueOwner* owner);
void audio_cue_owner_destroy(AudioCueOwner* owner);
