#pragma once
#include "audio_capture_pcm.hpp"
struct DomDocument;
enum AudioCapturePermission { AUDIO_CAPTURE_UNKNOWN, AUDIO_CAPTURE_PENDING, AUDIO_CAPTURE_ALLOWED,
    AUDIO_CAPTURE_DENIED, AUDIO_CAPTURE_UNAVAILABLE };
// trusted embedding seam; callbacks/context remain host-owned until document teardown.
// permission(request=true) may request OS consent; open never runs without both grants (D7.5.9).
struct AudioCaptureBackend {
    void* context;
    AudioCapturePermission (*permission)(void* context, bool request);
    void* (*open)(void* context, AudioCapturePcm* sink);
    bool (*lost)(void* context, void* device);
    void (*close)(void* context, void* device); // joins the producer before returning
};
const AudioCaptureBackend* audio_capture_native_backend();
struct AudioCaptureInfo {
    uint64_t token;
    const char* state;
    AudioCapturePcmInfo pcm;
    double start_ms;
    float monitor_gain;
    bool granted, monitor_lost;
};
// host-only grant, never inherited by another document or exposed to source code.
bool radiant_capture_grant(DomDocument* document, bool granted);
bool radiant_capture_set_backend(DomDocument* document, const AudioCaptureBackend* backend);
uint64_t radiant_capture_open(DomDocument* document);
bool radiant_capture_info(DomDocument* document, uint64_t token, AudioCaptureInfo* info);
unsigned radiant_capture_read(DomDocument* document, uint64_t token, float* output,
    unsigned frames, uint64_t* first_frame);
bool radiant_capture_monitor(DomDocument* document, uint64_t token, float gain);
bool radiant_capture_stop(DomDocument* document, uint64_t token);
bool radiant_capture_tick(DomDocument* document);
bool radiant_capture_pending(DomDocument* document);
