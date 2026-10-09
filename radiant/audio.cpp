#include "audio.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/mem.h"
#include "../lib/memtrack.h"
#include "../lib/arraylist.hpp"
#include <math.h>
#include <pthread.h>

struct AudioSlot { uint64_t token; RdtAudio* audio; };
struct DocumentAudio : DomDocumentResourceData {
    lam::ArrayList<AudioSlot> slots;
    DocumentAudio() : slots(MEM_CAT_RENDER, 0) {}
};

static void document_audio_destroy(DomDocumentResourceData* data) {
    auto* audio = static_cast<DocumentAudio*>(data);
    for (const AudioSlot& slot : audio->slots) rdt_audio_destroy(slot.audio);
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

uint64_t radiant_audio_open(DomDocument* doc, const void* bytes, size_t length) {
    if (!doc || !bytes || !length) return 0;
    DocumentAudio* audio = document_audio(doc, true);
    if (!audio) return 0;
    RdtAudio* player = rdt_audio_open_bytes(bytes, length);
    if (!player) return 0;
    // tokens never alias another document or a reused slot (D4.5.1v4).
    static pthread_mutex_t token_mutex = PTHREAD_MUTEX_INITIALIZER;
    static uint64_t next_token = 1;
    pthread_mutex_lock(&token_mutex);
    uint64_t token = next_token <= INT64_MAX ? next_token++ : 0;
    pthread_mutex_unlock(&token_mutex);
    if (token) {
        for (AudioSlot& slot : audio->slots) if (!slot.token) { slot = {token, player}; return token; }
        if (audio->slots.append({token, player})) return token;
    }
    rdt_audio_destroy(player);
    return 0;
}

bool radiant_audio_play(DomDocument* doc, uint64_t token, float volume) {
    AudioSlot* slot = document_audio_slot(doc, token);
    return slot && isfinite(volume) && volume >= 0 && volume <= 1 && rdt_audio_play(slot->audio, volume);
}
bool radiant_audio_pause(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot) return false;
    rdt_audio_pause(slot->audio);
    return true;
}
bool radiant_audio_close(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot) return false;
    rdt_audio_destroy(slot->audio);
    *slot = {};
    return true;
}
const char* radiant_audio_state(DomDocument* doc, uint64_t token) {
    AudioSlot* slot = document_audio_slot(doc, token);
    if (!slot) return nullptr;
    static const char* names[] = {"ready", "playing", "paused", "ended", "error"};
    return names[rdt_audio_state(slot->audio)];
}
