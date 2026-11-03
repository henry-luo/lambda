#pragma once
#include "audio_cues.hpp"

// D7.5.4: explicit output-free jobs use private virtual clocks, never document activation.
static constexpr unsigned AUDIO_RENDER_JOBS = 4, AUDIO_RENDER_STEP = 4096, AUDIO_RENDER_WAV_HEADER = 58;
static constexpr size_t AUDIO_RENDER_BYTES = 64u * 1024u * 1024u, AUDIO_RENDER_DOCUMENT_BYTES = 128u * 1024u * 1024u;
enum AudioRenderState { AUDIO_RENDER_READY, AUDIO_RENDER_RUNNING, AUDIO_RENDER_COMPLETE, AUDIO_RENDER_CANCELLED, AUDIO_RENDER_ERROR };
struct AudioRenderSource {
    uint64_t identity, asset;
    RdtAudioParameters parameters;
    float gain;
    char bus[128];
};
struct AudioRenderProvider {
    const AudioProviderOps* provider;
    uint64_t token;
    double position;
    char bus[128];
};
struct AudioRenderPlan {
    uint64_t frames;
    double scene_time, time_scale;
    AudioGraphConfig graph;
    unsigned source_count, event_count, provider_count;
    AudioRenderSource sources[AUDIO_CUE_TRACKS];
    AudioCueCommand events[AUDIO_CUE_CAPACITY];
    AudioRenderProvider providers[AUDIO_CUE_TRACKS];
};
struct AudioRenderSnapshot {
    uint64_t id, frames, rendered;
    size_t bytes;
    AudioRenderState state;
    char diagnostic[256];
    AudioCueSnapshot cues;
};
bool audio_render_open(DomDocument* document, uint64_t session, const AudioRenderPlan& plan, uint64_t* id, const char** error);
bool audio_render_step(DomDocument* document, uint64_t session, uint64_t id, unsigned frames, const char** error);
bool audio_render_snapshot(DomDocument* document, uint64_t session, uint64_t id, AudioRenderSnapshot* snapshot);
bool audio_render_cancel(DomDocument* document, uint64_t session, uint64_t id);
bool audio_render_close(DomDocument* document, uint64_t session, uint64_t id);
void audio_render_close_session(DomDocument* document, uint64_t session);
// copied stereo f32 little-endian chunks; the optional IEEE-float WAV header uses the same frames.
bool audio_render_read(DomDocument* document, uint64_t session, uint64_t id, uint64_t offset, unsigned frames,
    bool header, void* bytes, size_t capacity, size_t* length, const char** error);
const char* audio_render_state_name(AudioRenderState state);
