#include "audio.hpp"
#include "audio_bus.hpp"
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
struct AudioSlot { uint64_t token; RdtAudio* audio; MidiPlayer* midi; AudioVoiceId voice; DomNodeRef owner; };
struct DocumentAudio : DomDocumentResourceData {
    lam::ArrayList<AudioSlot> slots;
    bool activated;               // D7.5.4 sticky activation; never cleared
    AudioSubmixId submix;         // created with the first audible stream
    double last_tick_ms;          // offline output: frame clock at the previous tick
    double pending_frames;        // offline output: fractional frames not yet rendered
    DocumentAudio() : slots(MEM_CAT_RENDER, 0), activated(false), submix(0), last_tick_ms(-1), pending_frames(0) {}
};

static void audio_slot_close(AudioSlot* slot) {
    if (slot->audio) rdt_audio_destroy(slot->audio);
    // removing the voice first keeps the audio thread off the player being freed.
    audio_voice_remove(slot->voice);
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
uint64_t radiant_audio_open(DomDocument* doc, const void* bytes, size_t length) {
    if (!doc || !bytes || !length) return 0;
    DocumentAudio* audio = document_audio(doc, true);
    if (!audio) return 0;
    RdtAudio* player = rdt_audio_open_bytes(bytes, length);
    return player ? audio_slot_add(audio, {0, player, nullptr, {}}) : 0;
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

bool radiant_audio_play(DomDocument* doc, uint64_t token, float volume) {
    AudioSlot* slot = document_audio_slot(doc, token);
    // muted playback needs no activation (D7.5.4).
    return slot && slot->audio && isfinite(volume) && volume >= 0 && volume <= 1 &&
        (volume == 0 || radiant_document_may_start_audio(doc)) && rdt_audio_play(slot->audio, volume);
}
bool radiant_audio_pause(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->audio) return false;
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
    static const char* names[] = {"ready", "playing", "paused", "ended", "error"};
    return names[rdt_audio_state(slot->audio)];
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
    AudioSubmixId submix = radiant_document_submix(owner->doc);
    MidiPlayer* player = submix ? midi_player_open(events, count, duration, font, font_size, AUDIO_BUS_RATE) : nullptr;
    if (!player) return 0;
    // the session joins the bus at open, as its own device used to; no device means no session.
    const char* error = nullptr;
    AudioVoiceId voice = audio_voice_add(submix, midi_player_mix, player, &error);
    if (!voice) { log_error("audio: midi_open refused: %s", error ? error : "no voice"); midi_player_close(player); return 0; }
    return audio_slot_add(audio, {0, nullptr, player, voice, dom_node_ref(owner)});
}
bool radiant_midi_command(DomDocument* doc, uint64_t token, const char* command, double value) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi) return false;
    // opening and preparing a session is output-free; playing it is audible (D7.5.4).
    if (command && !strcmp(command, "play") && !radiant_document_may_start_audio(doc)) return false;
    if (audio_voice_lost(slot->voice)) midi_player_fail(slot->midi);
    // the lock keeps the audio thread out while the transport changes.
    if (!audio_voice_lock(slot->voice)) return false;
    bool done = midi_player_command(slot->midi, command, value);
    audio_voice_unlock(slot->voice);
    return done;
}
bool radiant_midi_info(DomDocument* doc, uint64_t token, MidiSnapshot* snapshot) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || !snapshot) return false;
    *snapshot = midi_player_info(slot->midi);
    return true;
}
AudioSubmixId radiant_document_submix(DomDocument* doc) {
    DocumentAudio* audio = document_audio(doc, true);
    if (audio && !audio->submix) audio->submix = audio_submix_create();
    return audio ? audio->submix : 0;
}
double radiant_audio_clock(DomDocument* doc) {
    DocumentAudio* audio = document_audio(doc);
    return audio ? audio_submix_clock(audio->submix) / (double)AUDIO_BUS_RATE : 0;
}

bool radiant_audio_tick(DomDocument* doc, double now_ms) {
    audio_bus_poll();
    DocumentAudio* audio = document_audio(doc);
    if (!audio) return false;
    for (AudioSlot& slot : audio->slots) if (slot.midi && audio_voice_lost(slot.voice)) midi_player_fail(slot.midi);
    // offline output: the sub-mix follows this document's frame clock while it has voices.
    double delta_ms = audio->last_tick_ms < 0 ? 0 : now_ms - audio->last_tick_ms;
    audio->last_tick_ms = now_ms;
    if (!audio_bus_offline() || !audio_submix_has_voices(audio->submix)) { audio->pending_frames = 0; return false; }
    // a resumed background document starts a new interval, not a catch-up burst.
    if (delta_ms < 0 || delta_ms > 1000) delta_ms = 0;
    audio->pending_frames += delta_ms * AUDIO_BUS_RATE / 1000.0;
    uint32_t frames = (uint32_t)audio->pending_frames;
    audio->pending_frames -= frames;
    if (frames) audio_submix_render(audio->submix, nullptr, frames);
    return true;
}

bool radiant_audio_frames_pending(DomDocument* doc) {
    DocumentAudio* audio = audio_bus_offline() ? document_audio(doc) : nullptr;
    return audio && audio_submix_has_voices(audio->submix);
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
