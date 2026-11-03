#include "audio_capture.hpp"
#include "audio.hpp"
#include "document_clock.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/mem.h"
#include <math.h>
#include <new>

enum CaptureState { CAPTURE_IDLE, CAPTURE_DENIED, CAPTURE_PENDING, CAPTURE_ACTIVE, CAPTURE_UNAVAILABLE,
    CAPTURE_STOPPED, CAPTURE_REVOKED, CAPTURE_LOST, CAPTURE_ERROR };
struct DocumentCapture : DomDocumentResourceData {
    DomDocument* document;
    AudioCaptureBackend backend;
    AudioCapturePcm* pcm;
    void* device;
    uint64_t token, storage;
    AudioVoiceId voice;
    AudioSubmixId submix;
    AudioCapturePcmInfo final;
    CaptureState state;
    double start_ms;
    float monitor_gain;
    bool granted, monitor_lost;
};
static bool capture_active(DocumentCapture* capture) { return capture->state == CAPTURE_PENDING || capture->state == CAPTURE_ACTIVE; }
static bool capture_work(DomDocumentResourceData* data) { return capture_active(static_cast<DocumentCapture*>(data)); }
static void capture_finish(DocumentCapture* capture, CaptureState state) {
    // exclude both native readers/producers before releasing either queue (D4.5.2 / D7.5.9).
    audio_voice_remove(capture->voice); capture->voice = 0; capture->monitor_gain = 0;
    if (capture->device) capture->backend.close(capture->backend.context, capture->device);
    capture->device = nullptr;
    if (capture->pcm) capture->final = audio_capture_pcm_info(capture->pcm);
    capture->final.monitoring = false; capture->final.queued = capture->final.monitor_queued = 0;
    audio_capture_pcm_destroy(capture->pcm); capture->pcm = nullptr;
    radiant_audio_storage_release(capture->document, capture->storage); capture->storage = 0;
    capture->state = state;
}
static void capture_destroy(DomDocumentResourceData* data) {
    auto* capture = static_cast<DocumentCapture*>(data); capture_finish(capture, CAPTURE_STOPPED);
    capture->~DocumentCapture(); mem_free(capture);
}
static DocumentCapture* document_capture(DomDocument* document, bool create = false) {
    if (!document) return nullptr;
    for (DomDocumentResource* resource = document->resources; resource; resource = resource->next)
        if (resource->destroy == capture_destroy) return static_cast<DocumentCapture*>(resource->data);
    if (!create || !radiant_audio_prepare_document(document)) return nullptr;
    void* storage = mem_alloc(sizeof(DocumentCapture), MEM_CAT_RENDER);
    auto* capture = storage ? new (storage) DocumentCapture{} : nullptr;
    if (!capture) return nullptr;
    capture->document = document; capture->backend = *audio_capture_native_backend();
    if (!dom_document_add_resource(document, capture, capture_destroy)) { capture_destroy(capture); return nullptr; }
    dom_document_set_resource_pending_work(document, capture, capture_work);
    return capture;
}
static void capture_start(DocumentCapture* capture) {
    capture->storage = radiant_audio_storage_acquire(capture->document, audio_capture_pcm_bytes());
    if (!capture->storage || !(capture->pcm = audio_capture_pcm_create())) { capture_finish(capture, CAPTURE_ERROR); return; }
    radiant_document_clock(capture->document, &capture->start_ms);
    capture->device = capture->backend.open(capture->backend.context, capture->pcm);
    if (!capture->device) { capture_finish(capture, CAPTURE_UNAVAILABLE); return; }
    capture->state = CAPTURE_ACTIVE;
}
bool radiant_capture_grant(DomDocument* document, bool granted) {
    auto* capture = document_capture(document, granted); if (!capture) return !granted;
    capture->granted = granted;
    if (!granted && capture_active(capture)) capture_finish(capture, CAPTURE_REVOKED);
    return true;
}
bool radiant_capture_set_backend(DomDocument* document, const AudioCaptureBackend* backend) {
    auto* capture = document_capture(document, true);
    if (!capture || capture_active(capture) || (backend && (!backend->permission || !backend->open || !backend->lost || !backend->close))) return false;
    capture->backend = *(backend ? backend : audio_capture_native_backend()); return true;
}
uint64_t radiant_capture_open(DomDocument* document) {
    auto* capture = document_capture(document, true); if (!capture || capture_active(capture)) return 0;
    capture_finish(capture, CAPTURE_IDLE); capture->final = {}; capture->monitor_lost = false; capture->start_ms = 0;
    capture->token = audio_bus_identity(INT64_MAX); if (!capture->token) return 0;
    if (!capture->granted) capture->state = CAPTURE_DENIED;
    else {
        auto permission = capture->backend.permission(capture->backend.context, true);
        if (permission == AUDIO_CAPTURE_ALLOWED) capture_start(capture);
        else capture->state = permission == AUDIO_CAPTURE_PENDING ? CAPTURE_PENDING :
            permission == AUDIO_CAPTURE_DENIED ? CAPTURE_DENIED : CAPTURE_UNAVAILABLE;
    }
    return capture->token;
}
bool radiant_capture_tick(DomDocument* document) {
    auto* capture = document_capture(document); if (!capture || !capture_active(capture)) return false;
    auto permission = capture->backend.permission(capture->backend.context, false);
    if (!capture->granted) capture_finish(capture, CAPTURE_REVOKED);
    else if (permission == AUDIO_CAPTURE_DENIED) capture_finish(capture, capture->state == CAPTURE_ACTIVE ? CAPTURE_REVOKED : CAPTURE_DENIED);
    else if (permission == AUDIO_CAPTURE_UNAVAILABLE || permission == AUDIO_CAPTURE_UNKNOWN) capture_finish(capture, CAPTURE_UNAVAILABLE);
    else if (capture->state == CAPTURE_PENDING && permission == AUDIO_CAPTURE_ALLOWED) capture_start(capture);
    if (capture->state == CAPTURE_ACTIVE) {
        if (capture->backend.lost(capture->backend.context, capture->device)) capture_finish(capture, CAPTURE_LOST);
        else if (audio_capture_pcm_info(capture->pcm).faulted) capture_finish(capture, CAPTURE_ERROR);
        else if (capture->voice && audio_voice_lost(capture->voice)) {
            audio_voice_remove(capture->voice); capture->voice = 0; capture->monitor_gain = 0; capture->monitor_lost = true;
            audio_capture_pcm_monitor(capture->pcm, false, 0);
        }
    }
    return capture_active(capture);
}
bool radiant_capture_pending(DomDocument* document) {
    auto* capture = document_capture(document); return capture && capture_active(capture);
}
bool radiant_capture_info(DomDocument* document, uint64_t token, AudioCaptureInfo* info) {
    radiant_capture_tick(document); auto* capture = document_capture(document);
    if (!capture || !token || capture->token != token || !info) return false;
    static const char* names[] = {"idle", "denied", "pending", "capturing", "unavailable", "stopped", "revoked", "lost", "error"};
    *info = {token, names[capture->state], capture->pcm ? audio_capture_pcm_info(capture->pcm) : capture->final,
        capture->start_ms, capture->monitor_gain, capture->granted, capture->monitor_lost}; return true;
}
unsigned radiant_capture_read(DomDocument* document, uint64_t token, float* output, unsigned frames, uint64_t* first) {
    radiant_capture_tick(document); auto* capture = document_capture(document);
    return capture && token && capture->token == token && capture->pcm ? audio_capture_pcm_read(capture->pcm, output, frames, first) : 0;
}
bool radiant_capture_monitor(DomDocument* document, uint64_t token, float gain) {
    radiant_capture_tick(document); auto* capture = document_capture(document);
    if (!capture || !token || capture->token != token || capture->state != CAPTURE_ACTIVE || !isfinite(gain) || gain < 0 || gain > 1 ||
        (gain > 0 && !radiant_document_may_start_audio(document))) return false;
    if (gain > 0 && !capture->submix) capture->submix = radiant_document_submix(document);
    // reserve a parked reader, then admit output before changing the current monitor.
    AudioVoiceId replacement = gain > 0 ? audio_voice_add(capture->submix, audio_capture_pcm_mix, capture->pcm, nullptr, 0, false) : 0;
    if (gain > 0 && !replacement) return false;
    if (replacement) {
        if (!audio_submix_lock(capture->submix)) { audio_voice_remove(replacement); return false; }
        if (!audio_submix_prepare_output(capture->submix, true, nullptr)) {
            audio_submix_unlock(capture->submix); audio_voice_remove(replacement); return false;
        }
        audio_voice_remove_locked(capture->voice);
        audio_capture_pcm_monitor(capture->pcm, true, gain);
        bool active = audio_voice_set_active_locked(replacement, true);
        audio_submix_unlock(capture->submix);
        if (!active) { audio_voice_remove(replacement); audio_capture_pcm_monitor(capture->pcm, false, 0); return false; }
    } else { audio_voice_remove(capture->voice); audio_capture_pcm_monitor(capture->pcm, false, 0); }
    capture->voice = replacement; capture->monitor_gain = gain; capture->monitor_lost = false; return true;
}
bool radiant_capture_stop(DomDocument* document, uint64_t token) {
    auto* capture = document_capture(document); if (!capture || !token || capture->token != token) return false;
    capture_finish(capture, CAPTURE_STOPPED); return true;
}
