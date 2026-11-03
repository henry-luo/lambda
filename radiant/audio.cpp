#include "audio.hpp"
#include "audio_bus.hpp"
#include "speech.hpp"
#include "audio_cue_owner.hpp"
#include "audio_capture.hpp"
#include "document_clock.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/arraylist.hpp"
#include "event.hpp"
#include <math.h>
#include <pthread.h>
#include <string.h>

// a MIDI session plays as a voice of its document's sub-mix (Radiant_Design_Speech §3.2).
static const float MIDI_DEFAULT_REVERB = 0.2f;
static constexpr size_t AUDIO_DECODED_SLOTS = 128, AUDIO_DOCUMENT_PCM_LIMIT = 512u * 1024u * 1024u;
struct AudioSlot {
    uint64_t token; RdtAudio* audio; MidiPlayer* midi; AudioVoiceId voice; DomNodeRef owner; AudioRouteId route; float reverb;
    AudioSubmixId submix;
    void* observer_owner;
    AudioPlayerObserver observer;
    AudioRouteRef routing;
    AudioVoiceLoss loss;
};
struct AudioMixAccess {
    AudioSubmixId id;
    bool valid;
    explicit AudioMixAccess(AudioSubmixId value) : id(value), valid(!value || audio_submix_lock(value)) {}
    ~AudioMixAccess() { if (id && valid) audio_submix_unlock(id); }
};
static bool audio_slot_notice(AudioSlot* slot, AudioPlayerNotice notice) {
    return slot->observer && slot->observer(slot->observer_owner, slot->token, notice);
}
struct AudioOwnerHook { void* owner; AudioOwnerPoll poll; };
struct DocumentAudio : DomDocumentResourceData {
    lam::ArrayList<AudioSlot> slots;
    bool activated;               // D7.5.4 sticky activation; never cleared
    AudioSubmixId submix;         // created with the first audible stream
    double last_tick_ms;          // offline output: frame clock at the previous tick
    double pending_frames;        // offline output: fractional frames not yet rendered
    AudioOwnerHook owners[8];      // the existing per-document Scene3D viewport bound
    struct { uint64_t id; size_t bytes; } storage[AUDIO_DECODED_SLOTS];
    DocumentAudio() : slots(MEM_CAT_RENDER, 0), activated(false), submix(0), last_tick_ms(-1), pending_frames(0), owners{}, storage{} {}
};

static void audio_slot_close(AudioSlot* slot) {
    audio_slot_notice(slot, AUDIO_PLAYER_CLOSE);
    // removing the voice first keeps the audio thread off the player being freed.
    audio_voice_remove(slot->voice);
    audio_submix_release_route(slot->submix, slot->routing);
    if (slot->audio) rdt_audio_destroy(slot->audio);
    if (slot->midi) midi_player_close(slot->midi);
    *slot = {};
}
static void document_audio_destroy(DomDocumentResourceData* data) {
    auto* audio = static_cast<DocumentAudio*>(data);
    for (AudioSlot& slot : audio->slots) audio_slot_close(&slot);
    // also retires voices other services (speech) still hold; their handles go stale.
    audio_submix_destroy(audio->submix);
    audio->~DocumentAudio();
    mem_free(audio);
}

static DocumentAudio* document_audio(DomDocument* doc, bool create = false) {
    if (!doc) return nullptr;
    for (DomDocumentResource* resource = doc->resources; resource; resource = resource->next)
        if (resource->destroy == document_audio_destroy) return static_cast<DocumentAudio*>(resource->data);
    if (!create) return nullptr;
    void* storage = mem_alloc(sizeof(DocumentAudio), MEM_CAT_RENDER);
    auto* audio = storage ? new (storage) DocumentAudio() : nullptr;
    if (audio && !dom_document_add_resource(doc, audio, document_audio_destroy)) {
        document_audio_destroy(audio);
        return nullptr;
    }
    return audio;
}

static AudioSlot* document_audio_slot(DomDocument* doc, uint64_t token) {
    DocumentAudio* audio = token ? document_audio(doc) : nullptr;
    if (audio) for (AudioSlot& slot : audio->slots) if (slot.token == token) return &slot;
    return nullptr;
}
bool radiant_audio_prepare_document(DomDocument* doc) { return document_audio(doc, true) != nullptr; }
bool radiant_audio_bind_owner(DomDocument* doc, void* owner, AudioOwnerPoll poll) {
    auto* audio = document_audio(doc, true);
    if (!audio || !owner || !poll) return false;
    for (auto& hook : audio->owners) if (hook.owner == owner) return hook.poll == poll;
    for (auto& hook : audio->owners) if (!hook.owner) { hook = {owner, poll}; return true; }
    return false;
}
void radiant_audio_unbind_owner(DomDocument* doc, void* owner) {
    auto* audio = document_audio(doc);
    if (audio) for (auto& hook : audio->owners) if (hook.owner == owner) hook = {};
}

static uint64_t audio_slot_add(DocumentAudio* audio, AudioSlot slot) {
    // tokens never alias another document or a reused slot (D4.5.1v4).
    static pthread_mutex_t token_mutex = PTHREAD_MUTEX_INITIALIZER;
    static uint64_t next_token = 1;
    pthread_mutex_lock(&token_mutex);
    uint64_t token = next_token <= INT64_MAX ? next_token++ : 0;
    pthread_mutex_unlock(&token_mutex);
    if (token) {
        slot.token = token;
        for (AudioSlot& empty : audio->slots) if (!empty.token) { empty = slot; return token; }
        if (audio->slots.append(slot)) return token;
    }
    audio_slot_close(&slot);
    return 0;
}
struct AudioDocumentUsage { size_t count, bytes; };
static bool document_audio_usage(DocumentAudio* audio, AudioDocumentUsage* usage) {
    AudioMixAccess access(audio->submix); if (!access.valid) return false;
    *usage = {};
    for (const auto& storage : audio->storage) if (storage.id) { usage->count++; usage->bytes += storage.bytes; }
    for (const AudioSlot& slot : audio->slots) if (slot.audio) {
        auto storage = rdt_audio_storage(slot.audio);
        bool shared = false;
        for (const AudioSlot& before : audio->slots) {
            if (&before == &slot) break;
            if (before.audio && rdt_audio_storage(before.audio).owner == storage.owner) { shared = true; break; }
        }
        size_t bytes = storage.player_bytes + (shared ? 0 : storage.shared_bytes);
        if (bytes > AUDIO_DOCUMENT_PCM_LIMIT - usage->bytes) return false;
        usage->bytes += bytes; usage->count++;
    }
    return true;
}
uint64_t radiant_audio_storage_acquire(DomDocument* doc, size_t bytes) {
    auto* audio = document_audio(doc, true); AudioDocumentUsage usage;
    if (!audio || !bytes || !document_audio_usage(audio, &usage) || usage.count >= AUDIO_DECODED_SLOTS ||
        bytes > AUDIO_DOCUMENT_PCM_LIMIT - usage.bytes) return 0;
    for (auto& storage : audio->storage) if (!storage.id) {
        uint64_t id = audio_bus_identity(); if (!id) return 0;
        storage = {id, bytes}; return id;
    }
    return 0;
}
void radiant_audio_storage_release(DomDocument* doc, uint64_t id) {
    auto* audio = document_audio(doc);
    if (audio && id) for (auto& storage : audio->storage) if (storage.id == id) { storage = {}; break; }
}
uint64_t radiant_audio_open(DomDocument* doc, const void* bytes, size_t length) {
    if (!doc || !bytes || !length) return 0;
    DocumentAudio* audio = document_audio(doc, true); AudioDocumentUsage usage;
    if (!audio || !document_audio_usage(audio, &usage) || usage.count == AUDIO_DECODED_SLOTS) return 0;
    RdtAudio* player = rdt_audio_open_bytes_limit(bytes, length, AUDIO_DOCUMENT_PCM_LIMIT - usage.bytes);
    return player ? audio_slot_add(audio, {0, player, nullptr, {}}) : 0;
}
size_t radiant_audio_available_pcm(DomDocument* doc) {
    auto* audio = document_audio(doc, true); AudioDocumentUsage usage;
    return audio && document_audio_usage(audio, &usage) && usage.count < AUDIO_DECODED_SLOTS ? AUDIO_DOCUMENT_PCM_LIMIT - usage.bytes : 0;
}
uint64_t radiant_audio_adopt(DomDocument* doc, RdtAudio* player) {
    if (!player) return 0;
    auto* audio = document_audio(doc, true);
    if (!audio || rdt_audio_storage_bytes(player) > radiant_audio_available_pcm(doc)) { rdt_audio_destroy(player); return 0; }
    return audio_slot_add(audio, {0, player, nullptr, {}});
}
const AudioProviderOps* radiant_audio_provider(const char* name) {
    static const AudioProviderOps providers[] = {
        {"midi", radiant_midi_route, radiant_midi_routing, radiant_midi_snapshot},
        {"speech", radiant_speech_route, radiant_speech_routing, radiant_speech_snapshot},
        {"video", radiant_media_route, radiant_media_routing, radiant_media_snapshot}};
    for (const auto& provider : providers) if (name && !strcmp(name, provider.name)) return &provider;
    return nullptr;
}
uint64_t radiant_audio_provider_snapshot(DomDocument* doc, const AudioProviderOps* provider,
    uint64_t token, double position, uint32_t frames, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    AudioProviderInfo info; size_t bytes = rdt_audio_pcm_storage_bytes(frames);
    if (!provider || !token || !isfinite(position) || position < 0 || !bytes || !provider->read(doc, token, &info)) {
        *error = "audio render: missing provider or invalid position/interval"; return 0;
    }
    if (info.loss != AUDIO_VOICE_OK || !strcmp(info.state, "idle") || !strcmp(info.state, "loading") ||
        !strcmp(info.state, "rendering") || !strcmp(info.state, "cancelled") || !strcmp(info.state, "error")) {
        *error = "audio render: provider is not prepared or has failed"; return 0;
    }
    if (bytes > radiant_audio_available_pcm(doc)) { *error = "audio render: provider snapshot exceeds document PCM/reader quota"; return 0; }
    auto* copy = provider->snapshot(doc, token, position, frames);
    if (!copy) { *error = "audio render: provider position or native snapshot preparation failed"; return 0; }
    auto id = radiant_audio_adopt(doc, copy);
    if (!id) *error = "audio render: provider snapshot ownership admission failed";
    return id;
}

// D7.5.4: judged by the document's own URL, so a network iframe in a local
// page needs its own activation; a host-built document without a URL is local.
static bool audio_document_local(DomDocument* doc) {
    const Url* url = doc->url.get();
    return !url || url->scheme == URL_SCHEME_FILE;
}
static bool audio_host_grant = false;
void radiant_audio_grant_activation(bool granted) { audio_host_grant = granted; }
void radiant_audio_note_activation(DomDocument* doc) {
    if (!doc || audio_document_local(doc)) return;
    DocumentAudio* audio = document_audio(doc, true);
    if (audio) audio->activated = true;
}
bool radiant_document_may_start_audio(DomDocument* doc) {
    if (!doc) return false;
    if (audio_host_grant || audio_document_local(doc)) return true;
    DocumentAudio* audio = document_audio(doc);
    return audio && audio->activated;
}

static bool audio_slot_poll(AudioSlot* slot) {
    bool armed = audio_slot_notice(slot, AUDIO_PLAYER_PENDING);
    AudioVoiceLoss loss = slot->routing && !audio_submix_route_ref_valid(slot->submix, slot->routing) ? AUDIO_VOICE_ROUTE_LOST : audio_voice_loss(slot->voice);
    if (!slot->voice && loss == AUDIO_VOICE_OK) return armed;
    bool lost = loss != AUDIO_VOICE_OK;
    bool playing = slot->audio ? rdt_audio_state(slot->audio) == RDT_AUDIO_PLAYING :
        slot->midi && midi_player_info(slot->midi).state == MIDI_PLAYING;
    if (lost || (!playing && !armed)) {
        if (lost) { audio_slot_notice(slot, AUDIO_PLAYER_LOST); armed = false; }
        // ended/paused voices must stop requesting offline frames; the decoded clip remains reusable.
        audio_voice_remove(slot->voice); slot->voice = 0;
        if (lost) {
            slot->loss = loss;
            if (slot->audio) rdt_audio_fail(slot->audio);
            if (slot->midi) midi_player_fail(slot->midi, loss == AUDIO_VOICE_ROUTE_LOST);
        }
    }
    return armed;
}
struct DecodedPlayerAccess {
    AudioMixAccess exclusion;
    RdtAudio* value = nullptr;
    AudioVoiceId voice = 0;
    explicit DecodedPlayerAccess(AudioSlot* slot, AudioRouteId route = UINT64_MAX) : exclusion(slot ? slot->submix : 0) {
        if (!slot || !slot->audio || !exclusion.valid) return;
        if (slot->voice && !(route == UINT64_MAX ? audio_voice_lock(slot->voice) : audio_voice_lock_route(slot->voice, route))) return;
        value = slot->audio; voice = slot->voice;
    }
    ~DecodedPlayerAccess() { if (voice) audio_voice_unlock(voice); }
    DecodedPlayerAccess(const DecodedPlayerAccess&) = delete;
    DecodedPlayerAccess& operator=(const DecodedPlayerAccess&) = delete;
};
static RdtAudio* audio_clone_slot(AudioSlot* slot) {
    DecodedPlayerAccess access(slot); return access.value ? rdt_audio_clone(access.value) : nullptr;
}
uint64_t radiant_audio_clone_ready(DomDocument* doc, uint64_t asset, const RdtAudioParameters* parameters, float gain) {
    auto* source = document_audio_slot(doc, asset); auto* audio = document_audio(doc); AudioDocumentUsage usage;
    if (!source || !source->audio || !audio || !isfinite(gain) || gain < 0 || gain > 1 ||
        !document_audio_usage(audio, &usage) || usage.count == AUDIO_DECODED_SLOTS) return 0;
    auto* clone = audio_clone_slot(source);
    if (!clone) return 0;
    if (!rdt_audio_parameters(clone, parameters) || !rdt_audio_gain(clone, gain) ||
        rdt_audio_storage(clone).player_bytes > AUDIO_DOCUMENT_PCM_LIMIT - usage.bytes) { rdt_audio_destroy(clone); return 0; }
    return audio_slot_add(audio, {0, clone, nullptr, 0, {}});
}
bool radiant_audio_identity(DomDocument* doc, uint64_t token, uint64_t identity) {
    DecodedPlayerAccess access(document_audio_slot(doc, token)); if (!access.value) return false;
    rdt_audio_identity(access.value, identity); return true;
}
bool radiant_audio_bind_lease_locked(DomDocument* doc, uint64_t token, AudioSubmixId submix,
    void* owner, AudioPlayerObserver observer, bool reserve_voice, AudioRouteId route, AudioPlayerLease* lease,
    bool* new_voice, bool* new_binding) {
    auto* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio || !lease || (slot->observer_owner && slot->observer_owner != owner)) return false;
    if (reserve_voice && !slot->voice) {
        slot->voice = audio_voice_add(submix, rdt_audio_mix, slot->audio, nullptr, route, false);
        if (!slot->voice) return false;
        if (new_voice) *new_voice = true;
    }
    if (new_binding) *new_binding = slot->observer_owner == nullptr;
    slot->submix = submix; slot->observer_owner = owner; slot->observer = observer; slot->route = route;
    *lease = {token, slot->audio, slot->voice}; return true;
}
void radiant_audio_unbind_lease_locked(DomDocument* doc, uint64_t token, void* owner, bool remove_voice) {
    auto* slot = document_audio_slot(doc, token); if (!slot || slot->observer_owner != owner) return;
    slot->observer_owner = nullptr; slot->observer = nullptr;
    if (remove_voice) {
        if (slot->voice) audio_voice_remove_locked(slot->voice);
        slot->voice = 0; slot->submix = 0; slot->route = 0;
    }
}

struct AudioCueOwner {
    DomDocument* document;
    AudioSubmixId submix;
    AudioCueQueue* queue;
    struct { uint64_t token; bool owned; } readers[AUDIO_DECODED_SLOTS];
    void* observer;
    AudioCueOwnerNotice notice;
};
AudioCueQueue* audio_cue_owner_queue(AudioCueOwner* owner) { return owner ? owner->queue : nullptr; }
void audio_cue_owner_unbind_locked(AudioCueOwner* owner, uint64_t token, bool remove_voice) {
    audio_cues_detach_locked(owner->queue, token, audio_submix_clock(owner->submix), AUDIO_CUE_CANCELLED);
    for (auto& reader : owner->readers) if (reader.token == token) reader = {};
    radiant_audio_unbind_lease_locked(owner->document, token, owner, remove_voice);
    audio_submix_refresh_work_locked(owner->submix);
}
static bool audio_cue_owner_notice(void* data, uint64_t token, AudioPlayerNotice notice) {
    auto* owner = (AudioCueOwner*)data;
    bool locked = audio_submix_lock(owner->submix);
    bool pending = audio_cues_pending_locked(owner->queue, token);
    if (notice != AUDIO_PLAYER_PENDING) {
        bool owned = false;
        for (const auto& reader : owner->readers) if (reader.token == token) owned = reader.owned;
        if (notice == AUDIO_PLAYER_LOST) audio_cues_detach_locked(owner->queue, token, audio_submix_clock(owner->submix), AUDIO_CUE_LOST);
        // a caller taking over an owned reader starts a new transport, outside its private render route.
        audio_cue_owner_unbind_locked(owner, token, notice == AUDIO_PLAYER_MANUAL && owned);
    }
    if (locked) audio_submix_unlock(owner->submix);
    if (notice != AUDIO_PLAYER_PENDING && owner->notice) owner->notice(owner->observer, token, notice);
    return pending;
}
AudioCueOwner* audio_cue_owner_create(DomDocument* document, AudioSubmixId submix, uint64_t epoch,
    void* observer, AudioCueOwnerNotice notice) {
    auto* owner = (AudioCueOwner*)mem_calloc(1, sizeof(AudioCueOwner), MEM_CAT_RENDER);
    if (!owner) return nullptr;
    owner->document = document; owner->submix = submix; owner->observer = observer; owner->notice = notice;
    owner->queue = audio_cues_create(submix, epoch);
    if (!owner->queue) { mem_free(owner); return nullptr; }
    return owner;
}
bool audio_cue_owner_bind_locked(AudioCueOwner* owner, uint64_t token, bool owned, bool voice,
    AudioRouteId route, AudioPlayerLease* lease, bool* new_voice, bool* new_binding) {
    unsigned slot = AUDIO_DECODED_SLOTS;
    for (unsigned i = 0; i < AUDIO_DECODED_SLOTS; i++) if (owner->readers[i].token == token) { slot = i; break; }
    if (slot == AUDIO_DECODED_SLOTS) for (unsigned i = 0; i < AUDIO_DECODED_SLOTS; i++) if (!owner->readers[i].token) { slot = i; break; }
    if (slot == AUDIO_DECODED_SLOTS || !radiant_audio_bind_lease_locked(owner->document, token, owner->submix,
        owner, audio_cue_owner_notice, voice, route, lease, new_voice, new_binding)) return false;
    owner->readers[slot] = {token, owned || owner->readers[slot].owned}; return true;
}
void audio_cue_owner_collect(AudioCueOwner* owner) {
    if (!owner || !audio_submix_lock(owner->submix)) return;
    uint64_t retired[AUDIO_CUE_CAPACITY]; unsigned count = audio_cues_collect_locked(owner->queue, retired, AUDIO_CUE_CAPACITY);
    for (unsigned i = 0; i < count; i++) audio_cue_owner_unbind_locked(owner, retired[i], false);
    audio_submix_unlock(owner->submix);
    for (unsigned i = 0; i < count; i++) radiant_audio_close(owner->document, retired[i]);
}
void audio_cue_owner_destroy(AudioCueOwner* owner) {
    if (!owner) return;
    audio_submix_unbind_driver(owner->submix, owner->queue);
    uint64_t retired[AUDIO_DECODED_SLOTS]; unsigned count = 0;
    bool locked = audio_submix_lock(owner->submix);
    for (auto& reader : owner->readers) if (reader.token) {
        if (reader.owned) retired[count++] = reader.token;
        radiant_audio_unbind_lease_locked(owner->document, reader.token, owner, false);
    }
    if (locked) audio_submix_unlock(owner->submix);
    for (unsigned i = 0; i < count; i++) radiant_audio_close(owner->document, retired[i]);
    audio_cues_destroy(owner->queue); mem_free(owner);
}
uint64_t radiant_audio_source_start(DomDocument* doc, uint64_t asset, uint64_t reuse,
    const RdtAudioParameters* parameters, float gain, AudioRouteId route) {
    auto* source = document_audio_slot(doc, asset);
    auto* destination = document_audio_slot(doc, reuse);
    if (!source || !source->audio || asset == reuse || (reuse && (!destination || !destination->audio)) ||
        !isfinite(gain) || gain < 0 || gain > 1 || (gain > 0 && !radiant_document_may_start_audio(doc))) return 0;
    auto submix = radiant_document_submix(doc);
    if (!audio_submix_route_valid(submix, route)) return 0;
    // a retained parked cue voice needs output admission just like a newly allocated live voice.
    if (!audio_submix_prepare_output(submix, true, nullptr)) return 0;
    struct Candidate {
        RdtAudio* value;
        ~Candidate() { rdt_audio_destroy(value); }
    } candidate{audio_clone_slot(source)};
    if (!candidate.value || !rdt_audio_parameters(candidate.value, parameters)) return 0;
    auto* audio = document_audio(doc); AudioDocumentUsage usage;
    auto storage = rdt_audio_storage(candidate.value);
    if (!audio || !document_audio_usage(audio, &usage) ||
        (!reuse && (usage.count == AUDIO_DECODED_SLOTS || storage.player_bytes > AUDIO_DOCUMENT_PCM_LIMIT - usage.bytes))) return 0;
    if (!reuse) {
        reuse = audio_slot_add(audio, {0, candidate.value, nullptr, 0, {}});
        candidate.value = nullptr;
        if (!reuse) return 0;
        destination = document_audio_slot(doc, reuse);
    }
    audio_slot_notice(destination, AUDIO_PLAYER_MANUAL); destination->submix = submix;
    audio_slot_poll(destination);
    if (!destination->voice) {
        const char* error = nullptr;
        destination->voice = audio_voice_add(submix, rdt_audio_mix, destination->audio, &error, route);
        if (!destination->voice) {
            // a new slot is still silent; an existing cursor remains unchanged on failed admission.
            if (!candidate.value) audio_slot_close(destination);
            return 0;
        }
    }
    DecodedPlayerAccess access(destination, route);
    if (!access.value) {
        if (!candidate.value) audio_slot_close(destination);
        return 0;
    }
    auto* prepared = candidate.value ? candidate.value : destination->audio;
    bool started = rdt_audio_play(prepared, gain);
    if (started && candidate.value) rdt_audio_swap(destination->audio, candidate.value);
    if (started) audio_voice_set_active_locked(destination->voice, true);
    if (started) destination->route = route;
    return started ? reuse : 0;
}
bool radiant_audio_graph_input_audible(AudioSubmixId submix, AudioGraphId graph, AudioRouteId route, bool active, double gain) {
    return active && gain > 0 && audio_submix_graph_has_route(submix, graph, route);
}
bool radiant_audio_graph_may_edit(DomDocument* doc, AudioGraphId graph) {
    if (radiant_document_may_start_audio(doc)) return true;
    auto* audio = document_audio(doc); if (!audio) return true;
    AudioGraphSnapshot snapshot;
    if (audio_submix_graph_snapshot(audio->submix, graph, &snapshot) && snapshot.active) return false;
    // DSP tail state excludes dry provider inputs; gate those before staged controls publish (D7.5.4).
    for (const auto& slot : audio->slots) if (audio_submix_graph_has_route(audio->submix, graph, slot.route)) {
        if (slot.midi) {
            auto info = midi_player_info(slot.midi);
            if (radiant_audio_graph_input_audible(audio->submix, graph, slot.route, info.state == MIDI_PLAYING, info.volume)) return false;
        } else if (slot.audio) {
            RdtAudioInfo info = {};
            if (radiant_audio_info(doc, slot.token, &info) && radiant_audio_graph_input_audible(audio->submix, graph, slot.route,
                info.state == RDT_AUDIO_PLAYING || info.armed, info.gain)) return false;
        }
    }
    return !radiant_speech_graph_audible(doc, graph) && !radiant_media_graph_audible(doc, graph);
}
bool radiant_audio_graph_commit(DomDocument* doc, AudioGraphId graph, AudioGraph* prepared,
    const AudioPlayerRoute* routes, unsigned count, const char** error) {
    if (error) *error = nullptr;
    if (!doc || count > AUDIO_DECODED_SLOTS || (count && !routes)) return false;
    AudioRouteChange changes[AUDIO_DECODED_SLOTS]; unsigned living = 0;
    for (unsigned i = 0; i < count; i++) {
        auto* slot = document_audio_slot(doc, routes[i].token);
        if (!slot || !slot->audio) return false;
        RdtAudioInfo info = {};
        if (!radiant_audio_info(doc, slot->token, &info) || ((prepared || slot->route != routes[i].route) &&
            (info.state == RDT_AUDIO_PLAYING || info.armed) && info.gain > 0 && !radiant_document_may_start_audio(doc))) return false;
        if (slot->voice) changes[living++] = {slot->voice, routes[i].route};
    }
    auto* audio = document_audio(doc); if (!audio) return false;
    if (prepared && !radiant_audio_graph_may_edit(doc, graph)) return false;
    if (!audio_submix_graph_commit(audio->submix, graph, prepared, changes, living, error)) return false;
    for (unsigned i = 0; i < count; i++) document_audio_slot(doc, routes[i].token)->route = routes[i].route;
    return true;
}
bool radiant_audio_parameters(DomDocument* doc, uint64_t token, const RdtAudioParameters* parameters, float gain) {
    auto* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio) return false;
    bool armed = audio_slot_poll(slot);
    DecodedPlayerAccess access(slot);
    RdtAudioInfo before = {};
    if (!rdt_audio_info(access.value, &before) || !isfinite(gain) || gain > 1 || (gain < 0 && gain != -1) ||
        (gain >= 0 && (before.state == RDT_AUDIO_PLAYING || armed) && gain > 0 && gain != before.gain &&
         !radiant_document_may_start_audio(doc))) return false;
    // parameter publication never restarts a transport that ended during a buffer.
    return rdt_audio_parameters(access.value, parameters) && (gain < 0 || rdt_audio_gain(access.value, gain));
}
bool radiant_audio_info(DomDocument* doc, uint64_t token, RdtAudioInfo* info) {
    auto* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio) return false;
    bool armed = audio_slot_poll(slot);
    DecodedPlayerAccess access(slot);
    bool valid = rdt_audio_info(access.value, info); if (valid) info->armed = armed; return valid;
}
bool radiant_audio_usage(DomDocument* doc, size_t* players, size_t* bytes) {
    if (!players || !bytes) return false;
    AudioDocumentUsage usage = {}; auto* audio = document_audio(doc);
    if (audio && !document_audio_usage(audio, &usage)) return false;
    *players = usage.count; *bytes = usage.bytes; return true;
}
bool radiant_audio_play(DomDocument* doc, uint64_t token, float volume) {
    AudioSlot* slot = document_audio_slot(doc, token);
    // muted playback needs no activation (D7.5.4).
    if (!slot || !slot->audio || !isfinite(volume) || volume < 0 || volume > 1 ||
        (volume > 0 && !radiant_document_may_start_audio(doc))) return false;
    audio_slot_poll(slot);
    if (rdt_audio_state(slot->audio) == RDT_AUDIO_ERROR) return false;
    audio_slot_notice(slot, AUDIO_PLAYER_MANUAL);
    slot->submix = radiant_document_submix(doc);
    if (!audio_submix_prepare_output(slot->submix, true, nullptr)) return false;
    if (!slot->voice) {
        const char* error = nullptr;
        slot->voice = audio_voice_add(radiant_document_submix(doc), rdt_audio_mix, slot->audio, &error, slot->route);
        if (!slot->voice) {
            rdt_audio_fail(slot->audio);
            log_error("audio: decoded play refused: %s", error ? error : "no voice"); return false;
        }
    }
    DecodedPlayerAccess access(slot);
    bool started = rdt_audio_play(access.value, volume);
    if (started) audio_voice_set_active_locked(slot->voice, true);
    return started;
}
bool radiant_audio_pause(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio) return false;
    audio_slot_notice(slot, AUDIO_PLAYER_MANUAL);
    audio_voice_remove(slot->voice); slot->voice = 0;
    rdt_audio_pause(slot->audio);
    return true;
}
bool radiant_audio_close(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot) return false;
    audio_slot_close(slot);
    return true;
}
const char* radiant_audio_state(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio) return nullptr;
    audio_slot_poll(slot);
    return rdt_audio_state_name(rdt_audio_state(slot->audio));
}

uint64_t radiant_midi_open(DomElement* owner, const MidiEvent* events, size_t count,
    double duration, const void* font, size_t font_size) {
    if (!owner || !dom_element_is_connected(owner)) return 0;
    DocumentAudio* audio = document_audio(owner->doc, true);
    if (!audio) return 0;
    radiant_audio_prune(owner->doc);
    size_t active = 0;
    for (const AudioSlot& slot : audio->slots) if (slot.midi) active++;
    if (active >= 4) return 0;
    MidiPlayer* player = midi_player_open(events, count, duration, font, font_size, AUDIO_BUS_RATE);
    if (!player) return 0;
    // preparation is output-free; paused/ended transports retain the synth, not an idle device voice.
    return audio_slot_add(audio, {0, nullptr, player, 0, dom_node_ref(owner), 0, MIDI_DEFAULT_REVERB});
}
bool radiant_midi_command(DomDocument* doc, uint64_t token, const char* command, double value) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || !command || !isfinite(value)) return false;
    audio_slot_poll(slot);
    // opening and preparing a session is output-free; playing it is audible (D7.5.4).
    auto before = midi_player_info(slot->midi);
    if (((!strcmp(command, "play") && before.volume > 0) || (!strcmp(command, "volume") &&
        before.state == MIDI_PLAYING && value > 0 && value != before.volume)) && !radiant_document_may_start_audio(doc)) return false;
    // the reverb send belongs to the session's bus voice, not to the transport.
    if (!strcmp(command, "reverb")) {
        if (value < 0 || value > 1 || (slot->voice && !audio_voice_set_send(slot->voice, (float)value))) return false;
        slot->reverb = (float)value; return true;
    }
    if (!strcmp(command, "play") && !slot->voice && midi_player_info(slot->midi).state != MIDI_ERROR) {
        const char* error = nullptr;
        slot->submix = radiant_document_submix(doc);
        slot->voice = audio_voice_add(slot->submix, midi_player_mix, slot->midi, &error, slot->route);
        if (!slot->voice) return false;
        if (!audio_voice_set_send(slot->voice, slot->reverb)) { audio_voice_remove(slot->voice); slot->voice = 0; return false; }
    }
    // the lock keeps the audio thread out while the transport changes.
    if (slot->voice && !audio_voice_lock(slot->voice)) return false;
    bool done = midi_player_command(slot->midi, command, value);
    if (slot->voice) audio_voice_unlock(slot->voice);
    audio_slot_poll(slot);
    return done;
}
bool radiant_midi_info(DomDocument* doc, uint64_t token, MidiSnapshot* snapshot) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || !snapshot) return false;
    audio_slot_poll(slot);
    *snapshot = midi_player_info(slot->midi);
    snapshot->reverb = slot->reverb;
    return true;
}
RdtAudio* radiant_midi_snapshot(DomDocument* doc, uint64_t token, double position, uint32_t frames) {
    auto* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || !rdt_audio_pcm_storage_bytes(frames)) return nullptr;
    struct PreparedMidi { MidiPlayer* player = nullptr; ~PreparedMidi() { midi_player_close(player); } } copy;
    {
        AudioMixAccess access(slot->submix); if (!access.valid) return nullptr;
        copy.player = midi_player_clone(slot->midi, 0);
    }
    if (!copy.player || !midi_player_command(copy.player, "seek", position)) return nullptr;
    auto info = midi_player_info(copy.player);
    if (position < info.duration && !midi_player_command(copy.player, "play", 0)) return nullptr;
    auto* samples = rdt_audio_pcm_alloc(frames); if (!samples) return nullptr;
    // fixed preparation partitions make job stepping independent of synth control-block boundaries.
    for (uint32_t done = 0; done < frames;) {
        uint32_t count = frames - done < 4096 ? frames - done : 4096;
        midi_player_render(copy.player, samples + done * 2, count); done += count;
    }
    return rdt_audio_adopt_pcm(samples, frames);
}
bool radiant_midi_route(DomDocument* doc, uint64_t token, AudioRouteId expected, AudioRouteId route, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    auto fail = [&](const char* message) { *error = message; return false; };
    auto* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || slot->route != expected) return fail("MIDI route: missing provider or stale expected route");
    audio_slot_poll(slot); auto snapshot = midi_player_info(slot->midi);
    if (route && snapshot.state == MIDI_ERROR) return fail("MIDI route: failed session must be reopened");
    if (route != slot->route && snapshot.state == MIDI_PLAYING && snapshot.volume > 0 && !radiant_document_may_start_audio(doc))
        return fail("MIDI route: audible rewire requires document activation");
    if (slot->route == route && !slot->routing) return true;
    auto mix = radiant_document_submix(doc);
    if (!audio_submix_bind_route(mix, &slot->routing, slot->voice, route)) return fail("MIDI route: stale/foreign route or reference quota exceeded");
    slot->submix = mix; slot->route = route; return true;
}
bool radiant_midi_routing(DomDocument* doc, uint64_t token, AudioProviderInfo* info) {
    MidiSnapshot snapshot; if (!info || !radiant_midi_info(doc, token, &snapshot)) return false;
    auto* slot = document_audio_slot(doc, token);
    *info = {slot->route, slot->loss, midi_player_state_name(snapshot.state), snapshot.position, snapshot.volume,
        !slot->route || audio_submix_route_valid(slot->submix, slot->route)};
    return true;
}
AudioSubmixId radiant_document_submix(DomDocument* doc) {
    DocumentAudio* audio = document_audio(doc, true);
    if (audio && !audio->submix) audio->submix = audio_submix_create();
    if (audio && !audio_submix_has_work(audio->submix)) {
        // anchor before the first voice, so its first frame neither loses time nor consumes idle time.
        radiant_document_clock(doc, &audio->last_tick_ms); audio->pending_frames = 0;
    }
    return audio ? audio->submix : 0;
}
double radiant_audio_clock(DomDocument* doc) {
    DocumentAudio* audio = document_audio(doc);
    return audio ? audio_submix_clock(audio->submix) / (double)AUDIO_BUS_RATE : 0;
}
uint64_t radiant_audio_frame(DomDocument* doc) {
    auto* audio = document_audio(doc); return audio ? audio_submix_clock(audio->submix) : 0;
}

bool radiant_audio_tick(DomDocument* doc, double now_ms) {
    audio_bus_poll();
    bool capture_pending = radiant_capture_tick(doc);
    DocumentAudio* audio = document_audio(doc);
    if (!audio) return false;
    // a hook may retire its scene and clear its slot while main-thread sampling runs.
    for (unsigned i = 0; i < 8; i++) {
        auto hook = audio->owners[i]; if (hook.poll) hook.poll(hook.owner, now_ms);
    }
    radiant_media_audio_tick(doc);
    for (AudioSlot& slot : audio->slots) audio_slot_poll(&slot);
    // offline output: the sub-mix follows this document's frame clock while it has voices.
    double delta_ms = audio->last_tick_ms < 0 ? 0 : now_ms - audio->last_tick_ms;
    audio->last_tick_ms = now_ms;
    if (!audio_bus_offline() || !audio_submix_has_work(audio->submix)) { audio->pending_frames = 0; return capture_pending; }
    // a resumed background document starts a new interval, not a catch-up burst.
    if (delta_ms < 0 || delta_ms > 1000) delta_ms = 0;
    audio->pending_frames += delta_ms * AUDIO_BUS_RATE / 1000.0;
    uint32_t frames = (uint32_t)audio->pending_frames;
    audio->pending_frames -= frames;
    if (frames) audio_submix_render(audio->submix, nullptr, frames);
    return true;
}

bool radiant_audio_frames_pending(DomDocument* doc) {
    // device output still needs main-owner polling to retire ended readers and completed tails.
    DocumentAudio* audio = document_audio(doc);
    if (audio) for (AudioSlot& slot : audio->slots) audio_slot_poll(&slot);
    return radiant_capture_pending(doc) || (audio && audio_submix_has_work(audio->submix));
}

void radiant_audio_prune(DomDocument* doc) {
    DocumentAudio* audio = document_audio(doc);
    if (audio) for (AudioSlot& slot : audio->slots) if (slot.midi &&
        (!dom_node_ref_validate(doc, slot.owner) || !dom_element_is_connected(slot.owner.address->as_element())))
        audio_slot_close(&slot);
}
void radiant_audio_follow_rebuild(DomDocument* doc, DomNode* old_root, DomNode* new_root) {
    if (!old_root || !new_root || old_root == new_root) return;
    DocumentAudio* audio = document_audio(doc);
    if (audio) for (AudioSlot& slot : audio->slots) if (slot.midi && dom_node_ref_validate(doc, slot.owner))
        view_state_ref_follow_rebuild(doc, &slot.owner, old_root, new_root);
}
