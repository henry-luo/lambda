// audio_bus.cpp — the window audio bus and document sub-mixes
// (vibe/radiant/Radiant_Design_Speech.md §3.2, M2).
//
// The audio thread never blocks: it try-locks each sub-mix and voice and skips
// one it cannot take. The UI thread spins on those locks, which the audio
// thread holds only while it renders one buffer. Tables are fixed, so a slot
// the audio thread reads is never freed; generations make stale handles inert.

#include "audio_bus.hpp"
#include "audio_reverb.hpp"
#include "../lib/log.h"

#define MINIAUDIO_IMPLEMENTATION
#include "audio_miniaudio.hpp"

#include <stdlib.h>
#include <string.h>

enum { AUDIO_BUS_SUBMIXES = 16, AUDIO_BUS_VOICES = 96 };   // 4 MIDI sessions + speech per document
enum { MIX_CHUNK = AUDIO_GRAPH_CHUNK };   // one bound for graph and voice scratch
enum { VOICE_FREE, VOICE_LIVE, VOICE_LOST, VOICE_PARKED };
enum { LOCK_IDLE, LOCK_AUDIO, LOCK_UI };

struct BusSubmix {
    uint32_t generation;
    int live;             // atomic
    int lock;             // atomic, LOCK_*
    uint64_t clock;       // atomic; frames rendered
    AudioReverb* reverb;  // created by the first voice that sends to it; changed under `lock`
    struct { AudioGraphId id; AudioGraph* plan; } graphs[AUDIO_GRAPH_OWNERS];
    int tails;            // atomic, UI polling never reads callback-owned DSP state
    uint32_t live_voices; // atomic, distinct from retained lost voice handles
    AudioSampleDriver drivers[AUDIO_GRAPH_OWNERS];
    int timed_work;
    bool virtual_output;
    struct { AudioRouteRef id; AudioRouteId route; bool lost; } references[AUDIO_ROUTE_REFERENCES];
};
struct BusVoice {
    uint32_t generation;
    int state;            // atomic, VOICE_*
    int lock;             // atomic, LOCK_*
    uint32_t submix;      // slot index
    AudioVoiceRender render;
    void* source;
    float send;           // reverb send level 0..1; changed under `lock`
    AudioRouteId route;
    AudioVoiceLoss loss;
};
struct AudioBus {
    BusSubmix submixes[AUDIO_BUS_SUBMIXES];
    BusVoice voices[AUDIO_BUS_VOICES];
    ma_device device;
    bool device_ready;
    int device_lost;      // atomic, set by the device notification
    int offline;          // -1 until read from the environment
    uint32_t voice_count; // retained voice records, including lost handles
};
static AudioBus bus = {{}, {}, {}, false, 0, -1, 0};
static bool bus_has_work();

static bool lock_try(int* lock) {
    int idle = LOCK_IDLE;
    return __atomic_compare_exchange_n(lock, &idle, LOCK_AUDIO, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static void lock_ui(int* lock) {
    for (;;) {
        int idle = LOCK_IDLE;
        if (__atomic_compare_exchange_n(lock, &idle, LOCK_UI, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        ma_yield();
    }
}
static void lock_release(int* lock) { __atomic_store_n(lock, LOCK_IDLE, __ATOMIC_RELEASE); }

static uint64_t handle_of(uint32_t index, uint32_t generation) { return ((uint64_t)generation << 32) | (index + 1); }
static BusSubmix* submix_at(AudioSubmixId id, uint32_t* index = nullptr) {
    uint32_t slot = (uint32_t)(id & 0xFFFFFFFFu);
    if (!slot || slot > AUDIO_BUS_SUBMIXES) return nullptr;
    BusSubmix* s = &bus.submixes[slot - 1];
    if (s->generation != (uint32_t)(id >> 32) || !__atomic_load_n(&s->live, __ATOMIC_ACQUIRE)) return nullptr;
    if (index) *index = slot - 1;
    return s;
}
static BusVoice* voice_at(AudioVoiceId id) {
    uint32_t slot = (uint32_t)(id & 0xFFFFFFFFu);
    if (!slot || slot > AUDIO_BUS_VOICES) return nullptr;
    BusVoice* v = &bus.voices[slot - 1];
    if (v->generation != (uint32_t)(id >> 32) || __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VOICE_FREE) return nullptr;
    return v;
}
static bool voice_lose(BusVoice* voice, AudioVoiceLoss reason) {
    int state = __atomic_load_n(&voice->state, __ATOMIC_ACQUIRE);
    if (state != VOICE_LIVE && state != VOICE_PARKED) return false;
    voice->loss = reason; __atomic_store_n(&voice->state, VOICE_LOST, __ATOMIC_RELEASE);
    if (state == VOICE_LIVE) __atomic_fetch_sub(&bus.submixes[voice->submix].live_voices, 1, __ATOMIC_RELEASE);
    return true;
}
uint64_t audio_bus_identity(uint64_t maximum) {
    static uint64_t next = 1;
    uint64_t value = __atomic_load_n(&next, __ATOMIC_RELAXED);
    do { if (!value || value > maximum || value == UINT64_MAX) return 0; }
    while (!__atomic_compare_exchange_n(&next, &value, value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return value;
}
static unsigned graph_owner(BusSubmix* mix, AudioGraphId id) {
    for (unsigned i = 0; i < AUDIO_GRAPH_OWNERS; i++) if (id && mix->graphs[i].id == id) return i;
    return AUDIO_GRAPH_OWNERS;
}
static AudioGraph* graph_for_route(BusSubmix* mix, AudioRouteId route) {
    unsigned flat = (unsigned)(route & 255);
    if (!flat || flat > AUDIO_GRAPH_OWNERS * AUDIO_GRAPH_BUSES) return nullptr;
    auto* graph = mix->graphs[(flat - 1) / AUDIO_GRAPH_BUSES].plan;
    return audio_graph_contains(graph, route) ? graph : nullptr;
}
static bool graph_references_valid(BusSubmix* mix, const AudioGraph* before, const AudioGraph* after, const char** error) {
    for (const auto& ref : mix->references) if (ref.id && !ref.lost && audio_graph_contains(before, ref.route) &&
        !audio_graph_contains(after, ref.route)) {
        if (error) *error = "audio graph: bus is retained by a provider; detach its route first";
        return false;
    }
    return true;
}
static bool submix_has_work(BusSubmix* mix) {
    return __atomic_load_n(&mix->tails, __ATOMIC_ACQUIRE) || __atomic_load_n(&mix->live_voices, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&mix->timed_work, __ATOMIC_ACQUIRE);
}
static void refresh_driver_work(BusSubmix* mix) {
    bool work = false;
    for (const auto& driver : mix->drivers) if (driver.owner) work |= driver.next(driver.owner) != UINT64_MAX;
    __atomic_store_n(&mix->timed_work, work, __ATOMIC_RELEASE);
}

// mixes one sub-mix into `out` (already holding earlier sub-mixes) and advances its clock.
// Each voice renders into its own buffer, which goes to the dry mix and, scaled by
// the voice's send, into the sub-mix reverb.
static void submix_mix(uint32_t index, float* out, uint32_t frames, bool clock_silence = false) {
    BusSubmix* s = &bus.submixes[index];
    if (!__atomic_load_n(&s->live, __ATOMIC_ACQUIRE) || !lock_try(&s->lock)) return;
    // prepared graphs do not start a document clock; explicit offline renders may request silence.
    if (!__atomic_load_n(&s->live, __ATOMIC_ACQUIRE) || (!clock_silence && (s->virtual_output || !submix_has_work(s)))) { lock_release(&s->lock); return; }
    uint64_t clock = __atomic_load_n(&s->clock, __ATOMIC_ACQUIRE);
    float voice[MIX_CHUNK * 2], send[MIX_CHUNK * 2];
    for (uint32_t done = 0; done < frames;) {
        uint64_t now = clock + done;
        for (auto& driver : s->drivers) if (driver.owner) driver.apply(driver.owner, now);
        uint32_t count = frames - done < MIX_CHUNK ? frames - done : MIX_CHUNK;
        for (const auto& driver : s->drivers) if (driver.owner) {
            uint64_t next = driver.next(driver.owner);
            if (next > now && next - now < count) count = (uint32_t)(next - now);
        }
        float* mix = out + (size_t)done * 2;
        for (auto& owner : s->graphs) audio_graph_begin(owner.plan, count);
        if (s->reverb) memset(send, 0, (size_t)count * 2 * sizeof(float));
        for (BusVoice& v : bus.voices) {
            // free records may be reassigned on the main owner; inspect their metadata only under the voice lock.
            if (__atomic_load_n(&v.state, __ATOMIC_ACQUIRE) != VOICE_LIVE || !lock_try(&v.lock)) continue;
            // re-read under the lock: the UI thread may have retired the voice meanwhile.
            if (__atomic_load_n(&v.state, __ATOMIC_ACQUIRE) == VOICE_LIVE && v.submix == index) {
                memset(voice, 0, (size_t)count * 2 * sizeof(float));
                v.render(v.source, voice, count, clock + done);
                if (!v.route) for (uint32_t i = 0; i < count * 2; i++) mix[i] += voice[i];
                else audio_graph_input(graph_for_route(s, v.route), v.route, voice, count);
                // named graphs own their complete FX path; legacy room sends must not bypass bus mute/gain.
                if (!v.route && s->reverb && v.send > 0) for (uint32_t i = 0; i < count * 2; i++) send[i] += voice[i] * v.send;
            }
            lock_release(&v.lock);
        }
        // the reverb runs even when nothing sends, so tails ring out after the music stops.
        if (s->reverb) audio_reverb_process(s->reverb, send, mix, count);
        bool tails = audio_reverb_active(s->reverb);
        for (auto& owner : s->graphs) { audio_graph_process(owner.plan, mix, count); tails |= audio_graph_active(owner.plan); }
        __atomic_store_n(&s->tails, tails, __ATOMIC_RELEASE);
        for (auto& driver : s->drivers) if (driver.owner && driver.after) driver.after(driver.owner, now + count);
        refresh_driver_work(s);
        done += count;
    }
    __atomic_store_n(&s->clock, clock + frames, __ATOMIC_RELEASE);
    lock_release(&s->lock);
}
static void clamp_mix(float* out, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2; i++) out[i] = out[i] > 1 ? 1 : out[i] < -1 ? -1 : out[i];
}
void audio_bus_mix(float* out, uint32_t frames) {
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    for (uint32_t i = 0; i < AUDIO_BUS_SUBMIXES; i++) submix_mix(i, out, frames);
    clamp_mix(out, frames);
}
static void bus_device_callback(ma_device*, void* output, const void*, ma_uint32 frames) {
    audio_bus_mix((float*)output, frames);
}
static void bus_device_notification(const ma_device_notification* notification) {
    // a stopped or interrupted device leaves no clock behind its voices; the UI thread retires it.
    if (notification->type == ma_device_notification_type_stopped ||
        notification->type == ma_device_notification_type_interruption_began)
        __atomic_store_n(&bus.device_lost, 1, __ATOMIC_RELEASE);
}

static void device_close() {
    if (!bus.device_ready) return;
    bus.device_ready = false;
    // uninit joins the audio thread, so no callback runs past this point.
    ma_device_uninit(&bus.device);
    __atomic_store_n(&bus.device_lost, 0, __ATOMIC_RELEASE);
}
static bool device_open(const char** error) {
    if (bus.device_ready) return true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32; config.playback.channels = 2; config.sampleRate = AUDIO_BUS_RATE;
    config.dataCallback = bus_device_callback; config.notificationCallback = bus_device_notification;
    if (ma_device_init(nullptr, &config, &bus.device) != MA_SUCCESS) { *error = "audio device unavailable"; return false; }
    bus.device_ready = true;
    // miniaudio's null backend advances a clock without any audible output.
    if (bus.device.pContext->backend == ma_backend_null || ma_device_start(&bus.device) != MA_SUCCESS) {
        device_close();
        *error = "audio device unavailable";
        return false;
    }
    log_debug("audio-bus: device open at %u Hz", AUDIO_BUS_RATE);
    return true;
}
bool audio_submix_lock(AudioSubmixId id) {
    auto* mix = submix_at(id); if (!mix) return false;
    lock_ui(&mix->lock); return true;
}
void audio_submix_unlock(AudioSubmixId id) {
    auto* mix = submix_at(id); if (mix) lock_release(&mix->lock);
}
bool audio_submix_prepare_output(AudioSubmixId id, bool required, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored;
    auto* mix = submix_at(id);
    return mix && (mix->virtual_output || !required || audio_bus_offline() || device_open(error));
}
bool audio_submix_bind_driver(AudioSubmixId id, const AudioSampleDriver& driver) {
    auto* mix = submix_at(id); if (!mix || !driver.owner || !driver.apply || !driver.next) return false;
    lock_ui(&mix->lock); bool bound = false;
    for (auto& slot : mix->drivers) if (slot.owner == driver.owner) { bound = true; break; }
    if (!bound) for (auto& slot : mix->drivers) if (!slot.owner) { slot = driver; bound = true; break; }
    refresh_driver_work(mix); lock_release(&mix->lock); return bound;
}
void audio_submix_unbind_driver(AudioSubmixId id, void* owner) {
    auto* mix = submix_at(id); if (!mix) return;
    lock_ui(&mix->lock);
    for (auto& slot : mix->drivers) if (slot.owner == owner) slot = {};
    refresh_driver_work(mix); lock_release(&mix->lock);
    if (!bus_has_work()) device_close();
}
void audio_submix_refresh_work_locked(AudioSubmixId id) { auto* mix = submix_at(id); if (mix) refresh_driver_work(mix); }

bool audio_bus_offline(void) {
    if (bus.offline < 0) {
        const char* output = getenv("LAMBDA_AUDIO_OUTPUT");
        bus.offline = output && !strcmp(output, "offline") ? 1 : 0;
    }
    return bus.offline == 1;
}
void audio_bus_set_offline(bool offline) {
    // switching modes under live voices would strand a device or a clock.
    if (bus.voice_count || bus_has_work()) { log_error("audio-bus: cannot change output mode with active voices/tails"); return; }
    bus.offline = offline ? 1 : 0;
}

AudioSubmixId audio_submix_create(bool virtual_output) {
    for (uint32_t i = 0; i < AUDIO_BUS_SUBMIXES; i++) {
        BusSubmix* s = &bus.submixes[i];
        if (__atomic_load_n(&s->live, __ATOMIC_ACQUIRE)) continue;
        lock_ui(&s->lock);
        s->generation++;
        s->virtual_output = virtual_output;
        __atomic_store_n(&s->clock, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&s->live_voices, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&s->live, 1, __ATOMIC_RELEASE);
        lock_release(&s->lock);
        return handle_of(i, s->generation);
    }
    log_error("audio-bus: all %d sub-mixes are in use", AUDIO_BUS_SUBMIXES);
    return 0;
}
void audio_submix_destroy(AudioSubmixId id) {
    uint32_t index = 0;
    BusSubmix* s = submix_at(id, &index);
    if (!s) return;
    lock_ui(&s->lock);
    for (auto& driver : s->drivers) {
        if (driver.owner && driver.lost) driver.lost(driver.owner, __atomic_load_n(&s->clock, __ATOMIC_ACQUIRE));
        driver = {};
    }
    __atomic_store_n(&s->timed_work, 0, __ATOMIC_RELEASE); lock_release(&s->lock);
    for (uint32_t i = 0; i < AUDIO_BUS_VOICES; i++) {
        BusVoice* v = &bus.voices[i];
        if (v->submix == index && __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VOICE_FREE)
            audio_voice_remove(handle_of(i, v->generation));
    }
    lock_ui(&s->lock);
    __atomic_store_n(&s->live, 0, __ATOMIC_RELEASE);
    AudioReverb* reverb = s->reverb;
    s->reverb = nullptr;
    AudioGraph* retired[AUDIO_GRAPH_OWNERS];
    for (unsigned i = 0; i < AUDIO_GRAPH_OWNERS; i++) { retired[i] = s->graphs[i].plan; s->graphs[i] = {}; }
    memset(s->references, 0, sizeof(s->references));
    __atomic_store_n(&s->tails, 0, __ATOMIC_RELEASE);
    lock_release(&s->lock);
    audio_reverb_destroy(reverb);
    for (auto* graph : retired) audio_graph_destroy(graph);
    if (!bus_has_work()) device_close();
}
uint64_t audio_submix_clock(AudioSubmixId id) {
    BusSubmix* s = submix_at(id);
    return s ? __atomic_load_n(&s->clock, __ATOMIC_ACQUIRE) : 0;
}
bool audio_submix_has_voices(AudioSubmixId id) {
    uint32_t index = 0;
    if (!submix_at(id, &index)) return false;
    for (BusVoice& v : bus.voices)
        if (v.submix == index && __atomic_load_n(&v.state, __ATOMIC_ACQUIRE) != VOICE_FREE) return true;
    return false;
}
bool audio_submix_has_work(AudioSubmixId id) {
    auto* mix = submix_at(id);
    return mix && submix_has_work(mix);
}
static bool bus_has_work() {
    for (auto& mix : bus.submixes) if (__atomic_load_n(&mix.live, __ATOMIC_ACQUIRE) && !mix.virtual_output && submix_has_work(&mix)) return true;
    return false;
}
AudioGraphId audio_submix_graph_open(AudioSubmixId id) {
    auto* mix = submix_at(id); if (!mix) return 0;
    for (unsigned i = 0; i < AUDIO_GRAPH_OWNERS; i++) if (!mix->graphs[i].id) {
        uint64_t value = audio_bus_identity(); if (!value) return 0;
        lock_ui(&mix->lock); mix->graphs[i].id = value; lock_release(&mix->lock); return value;
    }
    return 0;
}
void audio_submix_graph_close(AudioSubmixId id, AudioGraphId graph) {
    auto* mix = submix_at(id); if (!mix) return;
    unsigned slot = graph_owner(mix, graph); if (slot == AUDIO_GRAPH_OWNERS) return;
    lock_ui(&mix->lock); auto* retired = mix->graphs[slot].plan; mix->graphs[slot] = {};
    for (auto& ref : mix->references) if (ref.id && audio_graph_contains(retired, ref.route)) ref.lost = true;
    // owner teardown silences consumers before graph storage retires; no provider falls back to dry output.
    for (auto& voice : bus.voices) if (&bus.submixes[voice.submix] == mix && audio_graph_contains(retired, voice.route))
        voice_lose(&voice, AUDIO_VOICE_ROUTE_LOST);
    bool tails = audio_reverb_active(mix->reverb);
    for (auto& owner : mix->graphs) tails |= audio_graph_active(owner.plan);
    __atomic_store_n(&mix->tails, tails, __ATOMIC_RELEASE); lock_release(&mix->lock);
    audio_graph_destroy(retired);
    if (!bus_has_work()) device_close();
}
bool audio_submix_graph_prepare(AudioSubmixId id, AudioGraphId graph, const AudioGraphConfig& config,
    AudioGraph** prepared, const char** error) {
    auto* mix = submix_at(id); *prepared = nullptr;
    unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    if (slot == AUDIO_GRAPH_OWNERS) { if (error) *error = "audio graph: stale owner"; return false; }
    auto* before = mix->graphs[slot].plan;
    // configuration is immutable after publication; callback state is not inspected here.
    if (audio_graph_matches(before, config)) return true;
    *prepared = audio_graph_compile(config, slot, before, error, graph);
    // reject retained-bus removal before callers publish staged source parameters.
    if (*prepared && !graph_references_valid(mix, before, *prepared, error)) { audio_graph_destroy(*prepared); *prepared = nullptr; }
    return *prepared != nullptr;
}
AudioRouteId audio_submix_graph_route(AudioSubmixId id, AudioGraphId graph, const AudioGraph* prepared, const char* name) {
    auto* mix = submix_at(id); unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    return slot == AUDIO_GRAPH_OWNERS ? 0 : audio_graph_route(prepared ? prepared : mix->graphs[slot].plan, name);
}
bool audio_submix_graph_has_route(AudioSubmixId id, AudioGraphId graph, AudioRouteId route) {
    auto* mix = submix_at(id); unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    return slot < AUDIO_GRAPH_OWNERS && audio_graph_contains(mix->graphs[slot].plan, route);
}
bool audio_submix_route_valid(AudioSubmixId id, AudioRouteId route) {
    auto* mix = submix_at(id); return mix && (!route || graph_for_route(mix, route));
}
bool audio_submix_bind_route(AudioSubmixId id, AudioRouteRef* reference, AudioVoiceId voice_id, AudioRouteId route) {
    auto* mix = submix_at(id); auto* voice = voice_id ? voice_at(voice_id) : nullptr;
    if (!mix || !reference || (voice_id && (!voice || &bus.submixes[voice->submix] != mix)) || (route && !graph_for_route(mix, route))) return false;
    lock_ui(&mix->lock); unsigned slot = AUDIO_ROUTE_REFERENCES;
    if (*reference) for (unsigned i = 0; i < AUDIO_ROUTE_REFERENCES; i++) if (mix->references[i].id == *reference) { slot = i; break; }
    if (*reference && slot == AUDIO_ROUTE_REFERENCES) { lock_release(&mix->lock); return false; }
    uint64_t next = *reference;
    if (route && !next) {
        for (unsigned i = 0; i < AUDIO_ROUTE_REFERENCES; i++) if (!mix->references[i].id) { slot = i; break; }
        if (slot == AUDIO_ROUTE_REFERENCES || !(next = audio_bus_identity())) { lock_release(&mix->lock); return false; }
    }
    if (voice) { lock_ui(&voice->lock); voice->route = route; lock_release(&voice->lock); }
    if (slot < AUDIO_ROUTE_REFERENCES) {
        auto& ref = mix->references[slot]; ref = {};
        if (route) { ref.id = next; ref.route = route; }
    }
    *reference = route ? next : 0; lock_release(&mix->lock); return true;
}
bool audio_submix_route_ref_valid(AudioSubmixId id, AudioRouteRef reference) {
    auto* mix = submix_at(id); if (!mix || !reference) return false;
    for (const auto& ref : mix->references) if (ref.id == reference) return !ref.lost && graph_for_route(mix, ref.route);
    return false;
}
void audio_submix_release_route(AudioSubmixId id, AudioRouteRef reference) {
    auto* mix = submix_at(id); if (!mix || !reference) return;
    lock_ui(&mix->lock); for (auto& ref : mix->references) if (ref.id == reference) { ref = {}; break; }
    lock_release(&mix->lock);
}
bool audio_submix_graph_commit(AudioSubmixId id, AudioGraphId graph, AudioGraph* prepared, const AudioRouteChange* changes, unsigned count, const char** error) {
    if (error) *error = nullptr;
    auto* mix = submix_at(id); unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    if (slot == AUDIO_GRAPH_OWNERS || count > AUDIO_BUS_VOICES || (count && !changes)) return false;
    auto* target = prepared ? prepared : mix->graphs[slot].plan;
    if (prepared && !audio_graph_can_commit(prepared, mix->graphs[slot].plan, slot, graph)) return false;
    if (!graph_references_valid(mix, mix->graphs[slot].plan, target, error)) return false;
    for (unsigned i = 0; i < count; i++) {
        auto* voice = voice_at(changes[i].voice);
        if (!voice || &bus.submixes[voice->submix] != mix || (changes[i].route && !audio_graph_contains(target, changes[i].route))) return false;
    }
    lock_ui(&mix->lock); AudioGraph* retired = nullptr;
    if (prepared) {
        retired = mix->graphs[slot].plan; audio_graph_adopt_state(prepared, retired); mix->graphs[slot].plan = prepared;
    }
    for (unsigned i = 0; i < count; i++) voice_at(changes[i].voice)->route = changes[i].route;
    lock_release(&mix->lock); audio_graph_destroy(retired); return true;
}
bool audio_submix_graph_snapshot(AudioSubmixId id, AudioGraphId graph, AudioGraphSnapshot* out) {
    auto* mix = submix_at(id); unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    if (slot == AUDIO_GRAPH_OWNERS || !out) return false;
    lock_ui(&mix->lock); audio_graph_snapshot(mix->graphs[slot].plan, out); lock_release(&mix->lock); return true;
}
void audio_submix_graph_reset(AudioSubmixId id, AudioGraphId graph) {
    auto* mix = submix_at(id); unsigned slot = mix ? graph_owner(mix, graph) : AUDIO_GRAPH_OWNERS;
    if (slot == AUDIO_GRAPH_OWNERS) return;
    lock_ui(&mix->lock); audio_graph_reset(mix->graphs[slot].plan);
    bool tails = audio_reverb_active(mix->reverb);
    for (auto& owner : mix->graphs) tails |= audio_graph_active(owner.plan);
    __atomic_store_n(&mix->tails, tails, __ATOMIC_RELEASE); lock_release(&mix->lock);
    if (!bus_has_work()) device_close();
}

AudioVoiceId audio_voice_add(AudioSubmixId id, AudioVoiceRender render, void* source, const char** error, AudioRouteId route, bool active) {
    const char* ignored = nullptr;
    if (!error) error = &ignored;
    uint32_t index = 0;
    if (!submix_at(id, &index) || !render) { *error = "no audio sub-mix"; return 0; }
    if (!audio_submix_route_valid(id, route)) { *error = "stale audio route"; return 0; }
    for (uint32_t i = 0; i < AUDIO_BUS_VOICES; i++) {
        BusVoice* v = &bus.voices[i];
        if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VOICE_FREE) continue;
        if (active && !audio_submix_prepare_output(id, true, error)) return 0;
        lock_ui(&v->lock);
        v->generation++;
        v->submix = index; v->render = render; v->source = source; v->send = 0; v->route = route;
        v->loss = AUDIO_VOICE_OK;
        __atomic_store_n(&v->state, active ? VOICE_LIVE : VOICE_PARKED, __ATOMIC_RELEASE);
        if (active) __atomic_fetch_add(&bus.submixes[index].live_voices, 1, __ATOMIC_RELEASE);
        lock_release(&v->lock);
        bus.voice_count++;
        return handle_of(i, v->generation);
    }
    *error = "too many audio streams";
    return 0;
}
void audio_voice_remove_locked(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (!v) return;
    BusSubmix* mix = &bus.submixes[v->submix];
    lock_ui(&v->lock);
    int before = __atomic_load_n(&v->state, __ATOMIC_ACQUIRE);
    __atomic_store_n(&v->state, VOICE_FREE, __ATOMIC_RELEASE);
    v->render = nullptr; v->source = nullptr;
    lock_release(&v->lock);
    if (before == VOICE_LIVE) __atomic_fetch_sub(&mix->live_voices, 1, __ATOMIC_RELEASE);
    // the last voice closes the device; the next one reopens it.
    if (bus.voice_count) bus.voice_count--;
}
void audio_voice_remove(AudioVoiceId id) {
    auto* voice = voice_at(id); if (!voice) return;
    auto* mix = &bus.submixes[voice->submix]; lock_ui(&mix->lock);
    audio_voice_remove_locked(id); lock_release(&mix->lock);
    if (!bus_has_work()) device_close();
}
bool audio_voice_set_active_locked(AudioVoiceId id, bool active) {
    auto* voice = voice_at(id); if (!voice) return false;
    int before = __atomic_load_n(&voice->state, __ATOMIC_ACQUIRE);
    if (before == VOICE_LOST) return false;
    if ((before == VOICE_LIVE) != active) {
        __atomic_store_n(&voice->state, active ? VOICE_LIVE : VOICE_PARKED, __ATOMIC_RELEASE);
        if (active) __atomic_fetch_add(&bus.submixes[voice->submix].live_voices, 1, __ATOMIC_RELEASE);
        else __atomic_fetch_sub(&bus.submixes[voice->submix].live_voices, 1, __ATOMIC_RELEASE);
    }
    return true;
}
bool audio_voice_try_lock(AudioVoiceId id) { auto* voice = voice_at(id); return voice && lock_try(&voice->lock); }
bool audio_voice_lock(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (!v) return false;
    lock_ui(&v->lock);
    return true;
}
bool audio_voice_lock_route(AudioVoiceId id, AudioRouteId route) {
    auto* voice = voice_at(id);
    if (!voice || (route && !graph_for_route(&bus.submixes[voice->submix], route)) || !audio_voice_lock(id)) return false;
    voice->route = route; return true;
}
void audio_voice_unlock(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (v) lock_release(&v->lock);
}
bool audio_voice_set_send(AudioVoiceId id, float level) {
    BusVoice* v = voice_at(id);
    if (!v || !(level >= 0 && level <= 1)) return false;
    BusSubmix* s = &bus.submixes[v->submix];
    // the reverb is allocated here on the UI thread, never on the audio thread.
    if (level > 0 && !s->reverb) {
        AudioReverb* reverb = audio_reverb_create(AUDIO_BUS_RATE);
        if (!reverb) return false;
        lock_ui(&s->lock);
        s->reverb = reverb;
        lock_release(&s->lock);
    }
    lock_ui(&v->lock);
    v->send = level;
    lock_release(&v->lock);
    return true;
}
float audio_voice_send(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    return v ? v->send : 0;
}

bool audio_voice_lost(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    return v && __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VOICE_LOST;
}
AudioVoiceLoss audio_voice_loss(AudioVoiceId id) {
    auto* voice = voice_at(id);
    return voice && __atomic_load_n(&voice->state, __ATOMIC_ACQUIRE) == VOICE_LOST ? voice->loss : AUDIO_VOICE_OK;
}

void audio_bus_poll(void) {
    if (!bus_has_work()) device_close();
    if (!bus.device_ready || !__atomic_load_n(&bus.device_lost, __ATOMIC_ACQUIRE)) return;
    audio_bus_retire_output();
}
void audio_bus_retire_output(void) {
    device_close();
    unsigned retired = 0;
    // a lost device cannot leave offline/frame work stranded behind a tail.
    for (auto& mix : bus.submixes) if (__atomic_load_n(&mix.live, __ATOMIC_ACQUIRE) && !mix.virtual_output) {
        lock_ui(&mix.lock); audio_reverb_reset(mix.reverb);
        for (auto& voice : bus.voices) if (&bus.submixes[voice.submix] == &mix) retired += voice_lose(&voice, AUDIO_VOICE_DEVICE_LOST);
        for (auto& driver : mix.drivers) if (driver.owner && driver.lost)
            driver.lost(driver.owner, __atomic_load_n(&mix.clock, __ATOMIC_ACQUIRE));
        refresh_driver_work(&mix);
        for (auto& owner : mix.graphs) audio_graph_reset(owner.plan);
        __atomic_store_n(&mix.live_voices, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&mix.tails, 0, __ATOMIC_RELEASE); lock_release(&mix.lock);
    }
    log_error("audio-bus: output device lost; %u device voices silenced", retired);
}

bool audio_submix_render(AudioSubmixId id, float* capture, uint32_t frames) {
    uint32_t index = 0;
    auto* mix = submix_at(id, &index);
    if (!mix || (!mix->virtual_output && !audio_bus_offline())) return false;
    float chunk[1024 * 2];
    for (uint32_t done = 0; done < frames;) {
        uint32_t count = frames - done < 1024 ? frames - done : 1024;
        float* out = capture ? capture + (size_t)done * 2 : chunk;
        memset(out, 0, (size_t)count * 2 * sizeof(float));
        submix_mix(index, out, count, true);
        clamp_mix(out, count);
        done += count;
    }
    return true;
}
