#pragma once
#include <stddef.h>
#include <stdint.h>

// prepared native plans: no DOM, script values or callback-owned allocations (D5.5.2).
static constexpr unsigned AUDIO_GRAPH_OWNERS = 8, AUDIO_GRAPH_BUSES = 16,
    AUDIO_GRAPH_SENDS = 4, AUDIO_GRAPH_INSERTS = 4, AUDIO_GRAPH_CHUNK = 256;
static constexpr size_t AUDIO_GRAPH_STORAGE_LIMIT = 8u * 1024u * 1024u;
typedef uint64_t AudioGraphId;
typedef uint64_t AudioRouteId;
enum AudioEffectKind { AUDIO_EFFECT_LOWPASS, AUDIO_EFFECT_HIGHPASS, AUDIO_EFFECT_EQ,
    AUDIO_EFFECT_DELAY, AUDIO_EFFECT_REVERB, AUDIO_EFFECT_COMPRESSOR, AUDIO_EFFECT_LIMITER, AUDIO_EFFECT_KINDS };
struct AudioEffectConfig {
    AudioEffectKind kind;
    float frequency, q, gain_db, seconds, feedback, wet, threshold, ratio, attack, release, makeup, ceiling;
    char sidechain[128];
};
struct AudioSendConfig { char bus[128]; float gain; };
struct AudioBusConfig {
    char id[128], parent[128];
    float gain, smoothing;
    bool mute, solo;
    unsigned send_count, insert_count;
    AudioSendConfig sends[AUDIO_GRAPH_SENDS];
    AudioEffectConfig inserts[AUDIO_GRAPH_INSERTS];
};
struct AudioGraphConfig { unsigned count; AudioBusConfig buses[AUDIO_GRAPH_BUSES]; };
struct AudioGraphMeter { double peak, rms, current_gain; uint64_t frames; };
struct AudioGraphSnapshot {
    AudioGraphConfig config;
    AudioRouteId routes[AUDIO_GRAPH_BUSES];
    AudioGraphMeter meters[AUDIO_GRAPH_BUSES];
    double send_gains[AUDIO_GRAPH_BUSES][AUDIO_GRAPH_SENDS];
    size_t bytes;
    bool active, faulted;
};
struct AudioGraph;
AudioEffectConfig audio_effect_default(AudioEffectKind kind);
const char* audio_effect_name(AudioEffectKind kind);
AudioBusConfig audio_graph_bus_default(const char* id);
bool audio_graph_config_equal(const AudioGraphConfig& a, const AudioGraphConfig& b);
AudioGraph* audio_graph_compile(const AudioGraphConfig& config, unsigned owner_slot, const AudioGraph* previous,
    const char** error, AudioGraphId owner_identity = 0);
bool audio_graph_matches(const AudioGraph* graph, const AudioGraphConfig& config);
bool audio_graph_can_commit(const AudioGraph* graph, const AudioGraph* previous, unsigned slot, AudioGraphId owner);
void audio_graph_reset(AudioGraph* graph);
void audio_graph_destroy(AudioGraph* graph);
// called while the submix reader is excluded; compatible buses/inserts keep their history.
void audio_graph_adopt_state(AudioGraph* graph, AudioGraph* previous);
AudioRouteId audio_graph_route(const AudioGraph* graph, const char* name);
bool audio_graph_contains(const AudioGraph* graph, AudioRouteId route);
void audio_graph_snapshot(const AudioGraph* graph, AudioGraphSnapshot* snapshot);
bool audio_graph_active(const AudioGraph* graph);
void audio_graph_begin(AudioGraph* graph, unsigned frames);
bool audio_graph_input(AudioGraph* graph, AudioRouteId route, const float* input, unsigned frames);
void audio_graph_process(AudioGraph* graph, float* output, unsigned frames);
