#include "audio_graph.hpp"
#include "audio_bus.hpp"
#include "audio_reverb.hpp"
#include "audio_miniaudio.hpp"
#include "../lib/mem.h"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

struct AudioInsert {
    AudioEffectConfig config;
    ma_biquad filter;
    ma_biquad_coefficient cache[8];
    AudioReverb* reverb;
    float* delay;
    unsigned delay_frames, cursor, nonzero;
    double attack, release, envelope, makeup;
};
struct AudioGraphNode {
    AudioBusConfig config;
    AudioRouteId route;
    unsigned parent, targets[AUDIO_GRAPH_SENDS], detectors[AUDIO_GRAPH_INSERTS];
    AudioInsert* inserts[AUDIO_GRAPH_INSERTS];
    AudioGraphMeter meter;
    double decay;
    double send_gains[AUDIO_GRAPH_SENDS];
    bool present, input_enabled, output_enabled;
    float samples[AUDIO_GRAPH_CHUNK * 2];
};
struct AudioGraph {
    AudioGraphConfig config;
    AudioGraphNode nodes[AUDIO_GRAPH_BUSES];
    unsigned slots[AUDIO_GRAPH_BUSES], order[AUDIO_GRAPH_BUSES], owner;
    size_t bytes;
    uint64_t identity, base;
    AudioGraphId owner_identity;
    bool active, faulted;
};
static constexpr unsigned NO_BUS = AUDIO_GRAPH_BUSES;
const char* audio_effect_name(AudioEffectKind kind) {
    static const char* names[] = {"lowpass", "highpass", "eq", "delay", "reverb", "compressor", "limiter"};
    return kind >= AUDIO_EFFECT_LOWPASS && kind < AUDIO_EFFECT_KINDS ? names[kind] : nullptr;
}
static bool graph_name(const char* text, bool empty = false) {
    size_t length = strnlen(text, 128); return length < 128 && (empty || length);
}
AudioEffectConfig audio_effect_default(AudioEffectKind kind) {
    AudioEffectConfig c = {}; c.kind = kind; c.frequency = 1000; c.q = .70710678f;
    c.seconds = .1f; c.feedback = .3f; c.wet = .5f; c.threshold = -18; c.ratio = 4;
    c.attack = .01f; c.release = .1f; c.ceiling = .98f; return c;
}
AudioBusConfig audio_graph_bus_default(const char* id) {
    AudioBusConfig bus = {}; bus.gain = 1; bus.smoothing = .01f;
    if (id) str_copy(bus.id, sizeof(bus.id), id, strlen(id));
    if (!id || strcmp(id, "master")) str_copy(bus.parent, sizeof(bus.parent), "master", 6);
    return bus;
}
static bool effect_equal(const AudioEffectConfig& a, const AudioEffectConfig& b) {
    return a.kind == b.kind && a.frequency == b.frequency && a.q == b.q && a.gain_db == b.gain_db &&
        a.seconds == b.seconds && a.feedback == b.feedback && a.wet == b.wet && a.threshold == b.threshold &&
        a.ratio == b.ratio && a.attack == b.attack && a.release == b.release && a.makeup == b.makeup &&
        a.ceiling == b.ceiling && !strcmp(a.sidechain, b.sidechain);
}
bool audio_graph_config_equal(const AudioGraphConfig& a, const AudioGraphConfig& b) {
    if (a.count != b.count || a.count > AUDIO_GRAPH_BUSES) return false;
    for (unsigned i = 0; i < a.count; i++) {
        const auto& x = a.buses[i]; const auto& y = b.buses[i];
        if (x.send_count > AUDIO_GRAPH_SENDS || y.send_count > AUDIO_GRAPH_SENDS ||
            x.insert_count > AUDIO_GRAPH_INSERTS || y.insert_count > AUDIO_GRAPH_INSERTS) return false;
        if (strcmp(x.id, y.id) || strcmp(x.parent, y.parent) || x.gain != y.gain || x.smoothing != y.smoothing ||
            x.mute != y.mute || x.solo != y.solo || x.send_count != y.send_count || x.insert_count != y.insert_count) return false;
        for (unsigned j = 0; j < x.send_count; j++) if (strcmp(x.sends[j].bus, y.sends[j].bus) || x.sends[j].gain != y.sends[j].gain) return false;
        for (unsigned j = 0; j < x.insert_count; j++) if (!effect_equal(x.inserts[j], y.inserts[j])) return false;
    }
    return true;
}
static unsigned graph_slot(const AudioGraph* graph, const char* name) {
    if (graph && name) for (unsigned i = 0; i < AUDIO_GRAPH_BUSES; i++)
        if (graph->nodes[i].present && !strcmp(graph->nodes[i].config.id, name)) return i;
    return NO_BUS;
}
static AudioRouteId graph_route_identity(unsigned owner, unsigned slot) {
    static uint64_t next = 1;
    uint64_t value = __atomic_load_n(&next, __ATOMIC_RELAXED);
    do { if (!value || value >= (UINT64_MAX >> 8)) return 0; }
    while (!__atomic_compare_exchange_n(&next, &value, value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return (value << 8) | (owner * AUDIO_GRAPH_BUSES + slot + 1);
}
static bool effect_valid(const AudioEffectConfig& c) {
    return c.kind < AUDIO_EFFECT_KINDS && c.kind >= AUDIO_EFFECT_LOWPASS && graph_name(c.sidechain, true) &&
        isfinite(c.frequency) && c.frequency >= 20 && c.frequency <= 22000 &&
        isfinite(c.q) && c.q >= .1f && c.q <= 20 && isfinite(c.gain_db) && c.gain_db >= -24 && c.gain_db <= 24 &&
        isfinite(c.seconds) && c.seconds >= 1.f / AUDIO_BUS_RATE && c.seconds <= 2 &&
        isfinite(c.feedback) && c.feedback >= 0 && c.feedback <= .95f && isfinite(c.wet) && c.wet >= 0 && c.wet <= 1 &&
        isfinite(c.threshold) && c.threshold >= -96 && c.threshold <= 0 && isfinite(c.ratio) && c.ratio >= 1 && c.ratio <= 20 &&
        isfinite(c.attack) && c.attack >= 0 && c.attack <= 2 && isfinite(c.release) && c.release >= .001f && c.release <= 10 &&
        isfinite(c.makeup) && c.makeup >= 0 && c.makeup <= 24 && isfinite(c.ceiling) && c.ceiling >= .001f && c.ceiling <= 1 &&
        (!*c.sidechain || c.kind == AUDIO_EFFECT_COMPRESSOR);
}
static ma_biquad_config effect_filter(const AudioEffectConfig& c) {
    double omega = 2 * M_PI * c.frequency / AUDIO_BUS_RATE, cosine = cos(omega), alpha = sin(omega) / (2 * c.q);
    double b0, b1, b2, a0 = 1 + alpha, a1 = -2 * cosine, a2 = 1 - alpha;
    if (c.kind == AUDIO_EFFECT_EQ) {
        double amplitude = pow(10, c.gain_db / 40.0);
        b0 = 1 + alpha * amplitude; b1 = -2 * cosine; b2 = 1 - alpha * amplitude;
        a0 = 1 + alpha / amplitude; a2 = 1 - alpha / amplitude;
    } else {
        double sign = c.kind == AUDIO_EFFECT_LOWPASS ? 1 : -1;
        b0 = b2 = (1 - sign * cosine) * .5;
        // low-pass has a positive middle numerator; high-pass has a negative one.
        b1 = sign * (1 - sign * cosine);
    }
    return ma_biquad_config_init(ma_format_f32, 2, b0, b1, b2, a0, a1, a2);
}
static AudioInsert* effect_create(const AudioEffectConfig& config, size_t* budget) {
    size_t bytes = sizeof(AudioInsert);
    if (config.kind == AUDIO_EFFECT_DELAY) bytes += (size_t)llround(config.seconds * AUDIO_BUS_RATE) * 2 * sizeof(float);
    if (config.kind == AUDIO_EFFECT_REVERB) bytes += audio_reverb_storage_bytes(AUDIO_BUS_RATE);
    if (bytes > AUDIO_GRAPH_STORAGE_LIMIT - *budget) return nullptr;
    auto* state = (AudioInsert*)mem_calloc(1, sizeof(AudioInsert), MEM_CAT_RENDER);
    if (!state) return nullptr;
    state->config = config; state->attack = config.attack > 0 ? exp(-1.0 / (config.attack * AUDIO_BUS_RATE)) : 0;
    state->release = exp(-1.0 / (config.release * AUDIO_BUS_RATE)); state->makeup = pow(10, config.makeup / 20.0);
    if (config.kind <= AUDIO_EFFECT_EQ) {
        auto filter = effect_filter(config); size_t needed;
        if (ma_biquad_get_heap_size(&filter, &needed) != MA_SUCCESS || needed > sizeof(state->cache) ||
            ma_biquad_init_preallocated(&filter, state->cache, &state->filter) != MA_SUCCESS) { mem_free(state); return nullptr; }
    } else if (config.kind == AUDIO_EFFECT_DELAY) {
        state->delay_frames = (unsigned)llround(config.seconds * AUDIO_BUS_RATE);
        state->delay = (float*)mem_calloc(state->delay_frames * 2, sizeof(float), MEM_CAT_RENDER);
        if (!state->delay) { mem_free(state); return nullptr; }
    } else if (config.kind == AUDIO_EFFECT_REVERB) {
        state->reverb = audio_reverb_create(AUDIO_BUS_RATE);
        if (!state->reverb) { mem_free(state); return nullptr; }
    }
    *budget += bytes; return state;
}
static void effect_destroy(AudioInsert* effect) {
    if (!effect) return;
    audio_reverb_destroy(effect->reverb); mem_free(effect->delay); mem_free(effect);
}
void audio_graph_destroy(AudioGraph* graph) {
    if (!graph) return;
    for (auto& node : graph->nodes) for (auto* effect : node.inserts) effect_destroy(effect);
    mem_free(graph);
}
AudioGraph* audio_graph_compile(const AudioGraphConfig& config, unsigned owner, const AudioGraph* previous, const char** error, AudioGraphId owner_identity) {
    auto fail = [&](const char* message) -> AudioGraph* { if (error) *error = message; return nullptr; };
    if (!config.count || config.count > AUDIO_GRAPH_BUSES || owner >= AUDIO_GRAPH_OWNERS) return fail("audio graph: bus/owner limit");
    for (unsigned i = 0; i < config.count; i++) {
        const auto& b = config.buses[i];
        if (!graph_name(b.id) || !graph_name(b.parent, true) || !isfinite(b.gain) || b.gain < 0 || b.gain > 4 ||
            !isfinite(b.smoothing) || b.smoothing < 0 || b.smoothing > 10 || b.send_count > AUDIO_GRAPH_SENDS || b.insert_count > AUDIO_GRAPH_INSERTS ||
            ((!strcmp(b.id, "master")) != (!*b.parent))) return fail("audio graph: invalid bus description");
        for (unsigned j = 0; j < i; j++) if (!strcmp(config.buses[j].id, b.id)) return fail("audio graph: duplicate bus ID");
        for (unsigned j = 0; j < b.send_count; j++) if (!graph_name(b.sends[j].bus) || !isfinite(b.sends[j].gain) || b.sends[j].gain < 0 || b.sends[j].gain > 1)
            return fail("audio graph: invalid send");
        for (unsigned j = 0; j < b.insert_count; j++) if (!effect_valid(b.inserts[j])) return fail("audio graph: invalid insert");
    }
    auto* graph = (AudioGraph*)mem_calloc(1, sizeof(AudioGraph), MEM_CAT_RENDER);
    if (!graph) return fail("audio graph: allocation failed");
    struct Guard { AudioGraph* value; ~Guard() { audio_graph_destroy(value); } } guard{graph};
    graph->config = config; graph->owner = owner; graph->bytes = sizeof(AudioGraph);
    graph->identity = graph_route_identity(owner, 0); graph->base = previous ? previous->identity : 0; graph->owner_identity = owner_identity;
    if (!graph->identity) return fail("audio graph: plan identity exhausted");
    bool used[AUDIO_GRAPH_BUSES] = {};
    for (unsigned i = 0; i < config.count; i++) {
        unsigned slot = graph_slot(previous, config.buses[i].id); graph->slots[i] = slot;
        if (slot != NO_BUS) used[slot] = true;
    }
    for (unsigned i = 0; i < config.count; i++) {
        auto& slot = graph->slots[i]; if (slot == NO_BUS) { slot = 0; while (used[slot]) slot++; used[slot] = true; }
        auto& node = graph->nodes[slot]; node.present = true; node.config = config.buses[i];
        unsigned old = graph_slot(previous, node.config.id);
        node.route = old == NO_BUS ? graph_route_identity(owner, slot) : previous->nodes[old].route;
        if (!node.route) return fail("audio graph: route identity exhausted");
        node.parent = NO_BUS; node.meter.current_gain = node.config.mute ? 0 : node.config.gain;
        for (unsigned j = 0; j < node.config.send_count; j++) node.send_gains[j] = node.config.sends[j].gain;
        node.decay = node.config.smoothing > 0 ? exp(-1.0 / (node.config.smoothing * AUDIO_BUS_RATE)) : 0;
        for (unsigned j = 0; j < node.config.insert_count; j++) {
            node.inserts[j] = effect_create(node.config.inserts[j], &graph->bytes);
            if (!node.inserts[j]) return fail("audio graph: insert allocation/storage limit");
        }
    }
    if (graph_slot(graph, "master") == NO_BUS) return fail("audio graph: missing master bus");
    bool edges[AUDIO_GRAPH_BUSES][AUDIO_GRAPH_BUSES] = {}, audio_edges[AUDIO_GRAPH_BUSES][AUDIO_GRAPH_BUSES] = {};
    bool any_solo = false;
    for (unsigned i = 0; i < config.count; i++) {
        unsigned slot = graph->slots[i]; auto& node = graph->nodes[slot]; const auto& b = node.config;
        any_solo |= b.solo;
        auto edge = [&](const char* name, unsigned* target, bool audio) {
            *target = graph_slot(graph, name); if (*target == NO_BUS || *target == slot) return false;
            edges[slot][*target] = true; if (audio) audio_edges[slot][*target] = true; return true;
        };
        if (*b.parent && !edge(b.parent, &node.parent, true)) return fail("audio graph: unknown/self parent");
        for (unsigned j = 0; j < b.send_count; j++) if (!edge(b.sends[j].bus, node.targets + j, true)) return fail("audio graph: unknown/self send");
        for (unsigned j = 0; j < b.insert_count; j++) {
            node.detectors[j] = NO_BUS; const char* name = b.inserts[j].sidechain;
            if (*name) {
                unsigned detector = graph_slot(graph, name);
                if (detector == NO_BUS || detector == slot) return fail("audio graph: unknown/self sidechain");
                node.detectors[j] = detector; edges[detector][slot] = true;
            }
        }
    }
    unsigned incoming[AUDIO_GRAPH_BUSES] = {}; bool emitted[AUDIO_GRAPH_BUSES] = {};
    for (unsigned i = 0; i < AUDIO_GRAPH_BUSES; i++) for (unsigned j = 0; j < AUDIO_GRAPH_BUSES; j++) incoming[j] += edges[i][j];
    for (unsigned n = 0; n < config.count; n++) {
        unsigned slot = 0;
        while (slot < AUDIO_GRAPH_BUSES && (!graph->nodes[slot].present || emitted[slot] || incoming[slot])) slot++;
        if (slot == AUDIO_GRAPH_BUSES) return fail("audio graph: routing/sidechain cycle");
        graph->order[n] = slot; emitted[slot] = true;
        for (unsigned j = 0; j < AUDIO_GRAPH_BUSES; j++) if (edges[slot][j]) incoming[j]--;
    }
    for (unsigned i = 0; i < config.count; i++) {
        unsigned slot = graph->slots[i]; bool selected = !any_solo;
        for (unsigned at = slot; at != NO_BUS; at = graph->nodes[at].parent) selected |= graph->nodes[at].config.solo;
        graph->nodes[slot].input_enabled = graph->nodes[slot].output_enabled = selected;
    }
    for (unsigned n = 0; n < config.count; n++) {
        unsigned slot = graph->order[n]; if (!graph->nodes[slot].output_enabled) continue;
        for (unsigned j = 0; j < AUDIO_GRAPH_BUSES; j++) if (audio_edges[slot][j]) graph->nodes[j].output_enabled = true;
    }
    guard.value = nullptr; return graph;
}
void audio_graph_adopt_state(AudioGraph* graph, AudioGraph* old) {
    if (!graph || !old) return;
    for (auto& node : graph->nodes) {
        if (!node.present) continue;
        unsigned slot = graph_slot(old, node.config.id); if (slot == NO_BUS) continue;
        auto& before = old->nodes[slot]; node.meter = before.meter;
        for (unsigned j = 0; j < node.config.send_count; j++) {
            node.send_gains[j] = 0;
            for (unsigned k = 0; k < before.config.send_count; k++)
                if (!strcmp(node.config.sends[j].bus, before.config.sends[k].bus)) node.send_gains[j] = before.send_gains[k];
        }
        for (unsigned i = 0; i < node.config.insert_count && i < before.config.insert_count; i++) {
            auto* fresh = node.inserts[i]; auto* retained = before.inserts[i];
            if (fresh->config.kind != retained->config.kind || fresh->delay_frames != retained->delay_frames) continue;
            // coefficients were compiled off-reader; compatible edits keep the filter/delay/reverb history.
            retained->config = fresh->config; retained->attack = fresh->attack; retained->release = fresh->release; retained->makeup = fresh->makeup;
            if (fresh->config.kind <= AUDIO_EFFECT_EQ) {
                retained->filter.b0 = fresh->filter.b0; retained->filter.b1 = fresh->filter.b1; retained->filter.b2 = fresh->filter.b2;
                retained->filter.a1 = fresh->filter.a1; retained->filter.a2 = fresh->filter.a2;
            }
            node.inserts[i] = retained; before.inserts[i] = fresh;
        }
    }
    graph->active = old->active;
}
AudioRouteId audio_graph_route(const AudioGraph* graph, const char* name) {
    unsigned slot = graph_slot(graph, name); return slot == NO_BUS ? 0 : graph->nodes[slot].route;
}
bool audio_graph_contains(const AudioGraph* graph, AudioRouteId route) {
    unsigned flat = (unsigned)(route & 255);
    if (!graph || !flat || flat > AUDIO_GRAPH_OWNERS * AUDIO_GRAPH_BUSES || (flat - 1) / AUDIO_GRAPH_BUSES != graph->owner) return false;
    const auto& node = graph->nodes[(flat - 1) % AUDIO_GRAPH_BUSES]; return node.present && node.route == route;
}
void audio_graph_snapshot(const AudioGraph* graph, AudioGraphSnapshot* out) {
    *out = {}; if (!graph) return;
    out->config = graph->config; out->bytes = graph->bytes; out->active = graph->active; out->faulted = graph->faulted;
    for (unsigned i = 0; i < graph->config.count; i++) {
        const auto& node = graph->nodes[graph->slots[i]]; out->routes[i] = node.route; out->meters[i] = node.meter;
        memcpy(out->send_gains[i], node.send_gains, sizeof(node.send_gains));
    }
}
bool audio_graph_active(const AudioGraph* graph) { return graph && graph->active && !graph->faulted; }
bool audio_graph_matches(const AudioGraph* graph, const AudioGraphConfig& config) {
    return graph && audio_graph_config_equal(graph->config, config);
}
bool audio_graph_can_commit(const AudioGraph* graph, const AudioGraph* previous, unsigned slot, AudioGraphId owner) {
    return graph && graph->owner == slot && graph->owner_identity == owner && graph->base == (previous ? previous->identity : 0);
}
void audio_graph_reset(AudioGraph* graph) {
    if (!graph) return;
    graph->active = graph->faulted = false;
    for (auto& node : graph->nodes) if (node.present) {
        node.meter = {}; node.meter.current_gain = node.config.mute ? 0 : node.config.gain;
        for (unsigned j = 0; j < node.config.send_count; j++) node.send_gains[j] = node.config.sends[j].gain;
        for (auto* effect : node.inserts) if (effect) {
            memset(effect->cache, 0, sizeof(effect->cache)); effect->cursor = effect->nonzero = 0; effect->envelope = 0;
            if (effect->delay) memset(effect->delay, 0, effect->delay_frames * 2 * sizeof(float));
            audio_reverb_reset(effect->reverb);
        }
    }
}
void audio_graph_begin(AudioGraph* graph, unsigned frames) {
    if (!graph || frames > AUDIO_GRAPH_CHUNK) return;
    for (auto& node : graph->nodes) if (node.present) memset(node.samples, 0, frames * 2 * sizeof(float));
}
bool audio_graph_input(AudioGraph* graph, AudioRouteId route, const float* input, unsigned frames) {
    if (!audio_graph_contains(graph, route) || frames > AUDIO_GRAPH_CHUNK) return false;
    auto& node = graph->nodes[((unsigned)(route & 255) - 1) % AUDIO_GRAPH_BUSES];
    if (node.input_enabled && !graph->faulted) for (unsigned i = 0; i < frames * 2; i++) node.samples[i] += input[i];
    return true;
}
static bool effect_process(AudioInsert* state, float* samples, const float* detector, unsigned frames) {
    const auto& c = state->config;
    if (c.kind <= AUDIO_EFFECT_EQ) {
        ma_biquad_process_pcm_frames(&state->filter, samples, samples, frames);
        bool active = false;
        for (unsigned i = 0; i < 2; i++) active |= fabsf(state->filter.pR1[i].f32) > 1e-12f || fabsf(state->filter.pR2[i].f32) > 1e-12f;
        return active;
    }
    if (c.kind == AUDIO_EFFECT_REVERB) {
        float wet[AUDIO_GRAPH_CHUNK * 2] = {}; audio_reverb_process(state->reverb, samples, wet, frames);
        for (unsigned i = 0; i < frames * 2; i++) samples[i] = samples[i] * (1 - c.wet) + wet[i] * c.wet;
        return audio_reverb_active(state->reverb);
    }
    for (unsigned f = 0; f < frames; f++) {
        if (c.kind == AUDIO_EFFECT_DELAY) {
            for (unsigned ch = 0; ch < 2; ch++) {
                auto& slot = state->delay[state->cursor * 2 + ch]; float before = slot, next = samples[f * 2 + ch] + before * c.feedback;
                if (fabsf(next) < 1e-12f) next = 0;
                state->nonzero -= slot != 0; slot = next; state->nonzero += slot != 0;
                samples[f * 2 + ch] = samples[f * 2 + ch] * (1 - c.wet) + before * c.wet;
            }
            if (++state->cursor == state->delay_frames) state->cursor = 0;
        } else if (c.kind == AUDIO_EFFECT_COMPRESSOR) {
            const float* signal = detector ? detector : samples;
            double level = fmax(fabs(signal[f * 2]), fabs(signal[f * 2 + 1]));
            double decay = level > state->envelope ? state->attack : state->release;
            state->envelope = level + (state->envelope - level) * decay;
            double db = state->envelope > 1e-20 ? 20 * log10(state->envelope) : -400;
            double reduction = fmax(0, db - c.threshold) * (1 - 1 / (double)c.ratio);
            float gain = (float)(pow(10, -reduction / 20) * state->makeup);
            samples[f * 2] *= gain; samples[f * 2 + 1] *= gain;
        } else if (c.kind == AUDIO_EFFECT_LIMITER) {
            double peak = fmax(fabs(samples[f * 2]), fabs(samples[f * 2 + 1]));
            float gain = peak > c.ceiling ? (float)(c.ceiling / peak) : 1;
            samples[f * 2] *= gain; samples[f * 2 + 1] *= gain;
        }
    }
    return state->nonzero != 0;
}
void audio_graph_process(AudioGraph* graph, float* output, unsigned frames) {
    if (!graph || graph->faulted || frames > AUDIO_GRAPH_CHUNK) return;
    graph->active = false;
    for (unsigned n = 0; n < graph->config.count; n++) {
        auto& node = graph->nodes[graph->order[n]]; auto& c = node.config;
        for (unsigned i = 0; i < c.insert_count; i++) {
            auto detector = node.detectors[i];
            graph->active |= effect_process(node.inserts[i], node.samples, detector == NO_BUS ? nullptr : graph->nodes[detector].samples, frames);
        }
        double peak = 0, energy = 0, target = c.mute ? 0 : c.gain;
        for (unsigned f = 0; f < frames; f++) {
            node.meter.current_gain = target + (node.meter.current_gain - target) * node.decay;
            for (unsigned ch = 0; ch < 2; ch++) {
                auto& value = node.samples[f * 2 + ch]; value = node.output_enabled ? (float)(value * node.meter.current_gain) : 0;
                if (!isfinite(value)) { graph->faulted = true; graph->active = false; return; }
                peak = fmax(peak, fabs(value)); energy += (double)value * value;
            }
        }
        node.meter.peak = peak; node.meter.rms = frames ? sqrt(energy / (frames * 2)) : 0;
        if (node.meter.frames > UINT64_MAX - frames) { graph->faulted = true; graph->active = false; return; }
        node.meter.frames += frames;
        for (unsigned j = 0; j <= c.send_count; j++) {
            unsigned target_bus = j == c.send_count ? node.parent : node.targets[j];
            float* destination = target_bus == NO_BUS ? output : graph->nodes[target_bus].samples;
            for (unsigned f = 0; f < frames; f++) {
                double gain = 1;
                if (j < c.send_count) {
                    auto& current = node.send_gains[j]; current = c.sends[j].gain + (current - c.sends[j].gain) * node.decay;
                    gain = current;
                }
                for (unsigned ch = 0; ch < 2; ch++) destination[f * 2 + ch] += (float)(node.samples[f * 2 + ch] * gain);
            }
        }
    }
}
