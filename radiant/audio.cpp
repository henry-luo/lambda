#include "audio.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/mem.h"
#include "../lib/memtrack.h"
#include "../lib/arraylist.hpp"
#include "event.hpp"
#include <math.h>
#include <pthread.h>

struct AudioSlot { uint64_t token; RdtAudio* audio; MidiPlayer* midi; DomNodeRef owner; };
struct DocumentAudio : DomDocumentResourceData {
    lam::ArrayList<AudioSlot> slots;
    DocumentAudio() : slots(MEM_CAT_RENDER, 0) {}
};

static void audio_slot_close(AudioSlot* slot) {
    if (slot->audio) rdt_audio_destroy(slot->audio);
    if (slot->midi) midi_player_close(slot->midi);
    *slot = {};
}
static void document_audio_destroy(DomDocumentResourceData* data) {
    auto* audio = static_cast<DocumentAudio*>(data);
    for (AudioSlot& slot : audio->slots) audio_slot_close(&slot);
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

bool radiant_audio_play(DomDocument* doc, uint64_t token, float volume) {
    AudioSlot* slot = document_audio_slot(doc, token);
    return slot && slot->audio && isfinite(volume) && volume >= 0 && volume <= 1 && rdt_audio_play(slot->audio, volume);
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
    MidiPlayer* player = midi_player_open(events, count, duration, font, font_size);
    return player ? audio_slot_add(audio, {0, nullptr, player, dom_node_ref(owner)}) : 0;
}
bool radiant_midi_command(DomDocument* doc, uint64_t token, const char* command, double value) {
    AudioSlot* slot = document_audio_slot(doc, token);
    return slot && slot->midi && midi_player_command(slot->midi, command, value);
}
bool radiant_midi_info(DomDocument* doc, uint64_t token, MidiSnapshot* snapshot) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot || !slot->midi || !snapshot) return false;
    *snapshot = midi_player_info(slot->midi);
    return true;
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
