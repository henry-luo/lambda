// audio_bus.cpp — the window audio bus and document sub-mixes
// (vibe/radiant/Radiant_Design_Speech.md §3.2, M2).
//
// The audio thread never blocks: it try-locks each sub-mix and voice and skips
// one it cannot take. The UI thread spins on those locks, which the audio
// thread holds only while it renders one buffer. Tables are fixed, so a slot
// the audio thread reads is never freed; generations make stale handles inert.

#include "audio_bus.hpp"
#include "../lib/log.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "../lib/miniaudio/miniaudio.h"

#include <stdlib.h>
#include <string.h>

enum { AUDIO_BUS_SUBMIXES = 16, AUDIO_BUS_VOICES = 96 };   // 4 MIDI sessions + speech per document
enum { VOICE_FREE, VOICE_LIVE, VOICE_LOST };
enum { LOCK_IDLE, LOCK_AUDIO, LOCK_UI };

struct BusSubmix {
    uint32_t generation;
    int live;             // atomic
    int lock;             // atomic, LOCK_*
    uint64_t clock;       // atomic; frames rendered
};
struct BusVoice {
    uint32_t generation;
    int state;            // atomic, VOICE_*
    int lock;             // atomic, LOCK_*
    uint32_t submix;      // slot index
    AudioVoiceRender render;
    void* source;
};
struct AudioBus {
    BusSubmix submixes[AUDIO_BUS_SUBMIXES];
    BusVoice voices[AUDIO_BUS_VOICES];
    ma_device device;
    bool device_ready;
    int device_lost;      // atomic, set by the device notification
    int offline;          // -1 until read from the environment
    uint32_t voice_count; // voices holding the device open
};
static AudioBus bus = {{}, {}, {}, false, 0, -1, 0};

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

// mixes one sub-mix into `out` (already holding earlier sub-mixes) and advances its clock.
static void submix_mix(uint32_t index, float* out, uint32_t frames) {
    BusSubmix* s = &bus.submixes[index];
    if (!__atomic_load_n(&s->live, __ATOMIC_ACQUIRE) || !lock_try(&s->lock)) return;
    uint64_t clock = __atomic_load_n(&s->clock, __ATOMIC_ACQUIRE);
    for (BusVoice& v : bus.voices) {
        if (v.submix != index || __atomic_load_n(&v.state, __ATOMIC_ACQUIRE) != VOICE_LIVE || !lock_try(&v.lock)) continue;
        // re-read under the lock: the UI thread may have retired the voice meanwhile.
        if (__atomic_load_n(&v.state, __ATOMIC_ACQUIRE) == VOICE_LIVE && v.submix == index) v.render(v.source, out, frames, clock);
        lock_release(&v.lock);
    }
    __atomic_store_n(&s->clock, clock + frames, __ATOMIC_RELEASE);
    lock_release(&s->lock);
}
static void clamp_mix(float* out, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2; i++) out[i] = out[i] > 1 ? 1 : out[i] < -1 ? -1 : out[i];
}
static void bus_device_callback(ma_device*, void* output, const void*, ma_uint32 frames) {
    float* out = (float*)output;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    for (uint32_t i = 0; i < AUDIO_BUS_SUBMIXES; i++) submix_mix(i, out, frames);
    clamp_mix(out, frames);
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

bool audio_bus_offline(void) {
    if (bus.offline < 0) {
        const char* output = getenv("LAMBDA_AUDIO_OUTPUT");
        bus.offline = output && !strcmp(output, "offline") ? 1 : 0;
    }
    return bus.offline == 1;
}
void audio_bus_set_offline(bool offline) {
    // switching modes under live voices would strand a device or a clock.
    if (bus.voice_count) { log_error("audio-bus: cannot change output mode with %u live voices", bus.voice_count); return; }
    bus.offline = offline ? 1 : 0;
}

AudioSubmixId audio_submix_create(void) {
    for (uint32_t i = 0; i < AUDIO_BUS_SUBMIXES; i++) {
        BusSubmix* s = &bus.submixes[i];
        if (__atomic_load_n(&s->live, __ATOMIC_ACQUIRE)) continue;
        lock_ui(&s->lock);
        s->generation++;
        __atomic_store_n(&s->clock, 0, __ATOMIC_RELEASE);
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
    for (uint32_t i = 0; i < AUDIO_BUS_VOICES; i++) {
        BusVoice* v = &bus.voices[i];
        if (v->submix == index && __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VOICE_FREE)
            audio_voice_remove(handle_of(i, v->generation));
    }
    lock_ui(&s->lock);
    __atomic_store_n(&s->live, 0, __ATOMIC_RELEASE);
    lock_release(&s->lock);
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

AudioVoiceId audio_voice_add(AudioSubmixId id, AudioVoiceRender render, void* source, const char** error) {
    const char* ignored = nullptr;
    if (!error) error = &ignored;
    uint32_t index = 0;
    if (!submix_at(id, &index) || !render) { *error = "no audio sub-mix"; return 0; }
    for (uint32_t i = 0; i < AUDIO_BUS_VOICES; i++) {
        BusVoice* v = &bus.voices[i];
        if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VOICE_FREE) continue;
        if (!audio_bus_offline() && !device_open(error)) return 0;
        lock_ui(&v->lock);
        v->generation++;
        v->submix = index; v->render = render; v->source = source;
        __atomic_store_n(&v->state, VOICE_LIVE, __ATOMIC_RELEASE);
        lock_release(&v->lock);
        bus.voice_count++;
        return handle_of(i, v->generation);
    }
    *error = "too many audio streams";
    return 0;
}
void audio_voice_remove(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (!v) return;
    lock_ui(&v->lock);
    __atomic_store_n(&v->state, VOICE_FREE, __ATOMIC_RELEASE);
    v->render = nullptr; v->source = nullptr;
    lock_release(&v->lock);
    // the last voice closes the device; the next one reopens it.
    if (bus.voice_count && --bus.voice_count == 0) device_close();
}
bool audio_voice_lock(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (!v) return false;
    lock_ui(&v->lock);
    return true;
}
void audio_voice_unlock(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    if (v) lock_release(&v->lock);
}
bool audio_voice_lost(AudioVoiceId id) {
    BusVoice* v = voice_at(id);
    return v && __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VOICE_LOST;
}

void audio_bus_poll(void) {
    if (!bus.device_ready || !__atomic_load_n(&bus.device_lost, __ATOMIC_ACQUIRE)) return;
    log_error("audio-bus: output device lost; %u voices silenced", bus.voice_count);
    device_close();
    for (BusVoice& v : bus.voices) {
        int live = VOICE_LIVE;
        __atomic_compare_exchange_n(&v.state, &live, VOICE_LOST, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    }
}

bool audio_submix_render(AudioSubmixId id, float* capture, uint32_t frames) {
    uint32_t index = 0;
    if (!audio_bus_offline() || !submix_at(id, &index)) return false;
    float chunk[1024 * 2];
    for (uint32_t done = 0; done < frames;) {
        uint32_t count = frames - done < 1024 ? frames - done : 1024;
        float* out = capture ? capture + (size_t)done * 2 : chunk;
        memset(out, 0, (size_t)count * 2 * sizeof(float));
        submix_mix(index, out, count);
        clamp_mix(out, count);
        done += count;
    }
    return true;
}
