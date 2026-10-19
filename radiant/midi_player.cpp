#include "midi_player.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include <math.h>
#include <string.h>
#include <limits.h>

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "../lib/tinysoundfont/tsf.h"

struct MidiKey { uint8_t velocity; bool down; };
struct MidiChannel { MidiKey keys[128]; bool sustain; };
struct MidiPlayer {
    tsf* synth;
    MidiEvent* events;
    size_t count, next;
    uint32_t sample_rate;
    int state;
    double position, duration, speed, volume;
    double anchor;
    uint64_t elapsed_frames;
    uint16_t mute, solo;
    MidiChannel channels[16];
};
static void midi_set_state(MidiPlayer* p, MidiState state) { __atomic_store_n(&p->state, state, __ATOMIC_RELEASE); }
static MidiState midi_state(MidiPlayer* p) { return (MidiState)__atomic_load_n(&p->state, __ATOMIC_ACQUIRE); }
static double midi_position(MidiPlayer* p) { double v; __atomic_load(&p->position, &v, __ATOMIC_ACQUIRE); return v; }
static void midi_set_position(MidiPlayer* p, double v) { __atomic_store(&p->position, &v, __ATOMIC_RELEASE); }
static bool midi_audible(MidiPlayer* p, unsigned channel) {
    return !(p->mute & (1u << channel)) && (!p->solo || (p->solo & (1u << channel)));
}
static void midi_silence(MidiPlayer* p, int channel = -1) {
    // TSF's public reset frees channels and its all-sounds-off has a release tail.
    // this pinned adapter retires voices without allocation or leaking tails into a seek.
    for (int i = 0; i < p->synth->voiceNum; i++) if (channel < 0 || p->synth->voices[i].playingChannel == channel)
        p->synth->voices[i].playingPreset = -1;
}
static void midi_restore_notes(MidiPlayer* p, unsigned ch) {
    if (!midi_audible(p, ch)) return;
    for (unsigned key = 0; key < 128; key++) if (p->channels[ch].keys[key].velocity) {
        tsf_channel_note_on(p->synth, ch, key, p->channels[ch].keys[key].velocity / 127.0f);
        if (!p->channels[ch].keys[key].down) tsf_channel_note_off(p->synth, ch, key);
    }
}
static void midi_event(MidiPlayer* p, const MidiEvent& e, bool sounding) {
    unsigned ch = e.status & 15, kind = e.status & 240;
    MidiChannel* channel = &p->channels[ch];
    if (kind == 0x90 && e.b) {
        channel->keys[e.a] = {e.b, true};
        if (sounding && midi_audible(p, ch)) tsf_channel_note_on(p->synth, ch, e.a, e.b / 127.0f);
    } else if (kind == 0x80 || kind == 0x90) {
        channel->keys[e.a].down = false;
        if (!channel->sustain) channel->keys[e.a].velocity = 0;
        if (sounding) tsf_channel_note_off(p->synth, ch, e.a);
    } else if (kind == 0xB0) {
        if (e.a == 64 || e.a == 121) {
            channel->sustain = e.a == 64 && e.b >= 64;
            if (!channel->sustain) for (MidiKey& key : channel->keys) if (!key.down) key.velocity = 0;
            if (e.a == 121) {
                tsf_channel_set_sustain(p->synth, ch, 0);
                tsf_channel_set_pitchwheel(p->synth, ch, 8192);
            }
        }
        if (e.a == 120 || e.a == 123) {
            // TSF's note_off_all bypasses sustain; issue ordinary key releases instead.
            for (unsigned key = 0; key < 128; key++) {
                MidiKey* held = &channel->keys[key];
                if (e.a == 123 && held->down && sounding) tsf_channel_note_off(p->synth, ch, key);
                held->down = false;
                if (e.a == 120 || !channel->sustain) held->velocity = 0;
            }
            if (e.a == 120) midi_silence(p, ch);
            return;
        }
        tsf_channel_midi_control(p->synth, ch, e.a, e.b);
    } else if (kind == 0xC0) {
        if (!tsf_channel_set_presetnumber(p->synth, ch, e.a, ch == 9)) midi_set_state(p, MIDI_ERROR);
    } else if (kind == 0xE0) tsf_channel_set_pitchwheel(p->synth, ch, e.a + 128 * e.b);
}
static void midi_restore(MidiPlayer* p, double target, bool sounding) {
    midi_silence(p);
    memset(p->channels, 0, sizeof(p->channels));
    for (unsigned ch = 0; ch < 16; ch++) {
        tsf_channel_midi_control(p->synth, ch, 121, 0);
        tsf_channel_set_sustain(p->synth, ch, 0);
        tsf_channel_set_pitchwheel(p->synth, ch, 8192);
        tsf_channel_set_presetnumber(p->synth, ch, 0, ch == 9);
    }
    p->next = 0;
    // replay controller state on the control thread, never in the audio callback.
    while (p->next < p->count && p->events[p->next].seconds < target) midi_event(p, p->events[p->next++], false);
    if (sounding) for (unsigned ch = 0; ch < 16; ch++) midi_restore_notes(p, ch);
    p->anchor = target; p->elapsed_frames = 0;
    midi_set_position(p, target);
}
void midi_player_render(MidiPlayer* p, float* output, uint32_t frames) {
    if (!output) return;
    memset(output, 0, (size_t)frames * 2 * sizeof(float));
    if (!p || midi_state(p) != MIDI_PLAYING) return;
    double position = midi_position(p), step = p->speed / p->sample_rate;
    uint32_t done = 0;
    while (done < frames && position < p->duration && midi_state(p) == MIDI_PLAYING) {
        while (p->next < p->count && p->events[p->next].seconds <= position + step * 0.000001)
            midi_event(p, p->events[p->next++], true);
        double boundary = p->next < p->count ? p->events[p->next].seconds : p->duration;
        if (boundary > p->duration) boundary = p->duration;
        double until = ceil((boundary - position) / step - 0.000001);
        uint32_t count = until <= 1 ? 1 : until < frames - done ? (uint32_t)until : frames - done;
        tsf_render_float(p->synth, output + done * 2, count, 0);
        for (uint32_t i = done * 2; i < (done + count) * 2; i++)
            output[i] = fmaxf(-1, fminf(1, output[i] * (float)p->volume));
        done += count;
        p->elapsed_frames += count;
        position = p->anchor + p->elapsed_frames * step;
    }
    if (position >= p->duration) { position = p->duration; midi_silence(p); midi_set_state(p, MIDI_ENDED); }
    if (midi_state(p) == MIDI_ERROR) { midi_silence(p); memset(output, 0, (size_t)frames * 2 * sizeof(float)); }
    midi_set_position(p, position);
}
void midi_player_mix(void* source, float* output, uint32_t frames, uint64_t) {
    float chunk[512 * 2];
    for (uint32_t done = 0; done < frames;) {
        uint32_t count = frames - done < 512 ? frames - done : 512;
        midi_player_render((MidiPlayer*)source, chunk, count);
        for (uint32_t i = 0; i < count * 2; i++) output[done * 2 + i] += chunk[i];
        done += count;
    }
}
void midi_player_fail(MidiPlayer* p) {
    if (p && midi_state(p) == MIDI_PLAYING) { midi_silence(p); midi_set_state(p, MIDI_ERROR); }
}
MidiPlayer* midi_player_open(const MidiEvent* events, size_t count, double duration,
    const void* font, size_t font_size, uint32_t sample_rate) {
    if (!font || font_size < 12 || font_size > 128 * 1024 * 1024 || !events || !count || count > 200000 ||
        !isfinite(duration) || duration <= 0 || duration > 86400 || sample_rate < 8000 || sample_rate > 192000) return nullptr;
    for (size_t i = 0; i < count; i++) {
        const MidiEvent& e = events[i]; unsigned kind = e.status & 240;
        if (!isfinite(e.seconds) || e.seconds < 0 || e.seconds > duration || (i && e.seconds < events[i-1].seconds) ||
            e.a > 127 || e.b > 127 || (kind != 0x80 && kind != 0x90 && kind != 0xB0 && kind != 0xC0 && kind != 0xE0)) return nullptr;
    }
    auto* p = (MidiPlayer*)mem_calloc(1, sizeof(MidiPlayer), MEM_CAT_RENDER);
    if (!p) return nullptr;
    p->events = (MidiEvent*)mem_alloc(count * sizeof(MidiEvent), MEM_CAT_RENDER);
    p->synth = tsf_load_memory(font, (int)font_size); // INT_CAST_OK: bounded soundfont byte length.
    if (!p->events || !p->synth || !tsf_set_max_voices(p->synth, 256)) { midi_player_close(p); return nullptr; }
    memcpy(p->events, events, count * sizeof(MidiEvent));
    p->count = count; p->duration = duration; p->speed = 1; p->volume = 0.8; p->sample_rate = sample_rate;
    tsf_set_output(p->synth, TSF_STEREO_INTERLEAVED, sample_rate, -6);
    // allocate every MIDI channel before the first callback, and verify the GM baseline bank.
    for (int ch = 0; ch < 16; ch++) if (!tsf_channel_set_presetnumber(p->synth, ch, 0, ch == 9)) {
        midi_player_close(p); return nullptr;
    }
    midi_restore(p, 0, false);
    return p;
}
void midi_player_close(MidiPlayer* p) {
    if (!p) return;
    if (p->synth) tsf_close(p->synth);
    mem_free(p->events); mem_free(p);
}
MidiSnapshot midi_player_info(MidiPlayer* p) {
    if (!p) return {MIDI_ERROR, 0, 0, 1, 0, 0, 0};
    return {midi_state(p), midi_position(p), p->duration, p->speed, p->volume, p->mute, p->solo};
}
bool midi_player_command(MidiPlayer* p, const char* command, double value) {
    if (!p || !command || !isfinite(value) || midi_state(p) == MIDI_ERROR) return false;
    bool play = !strcmp(command, "play"), pause = !strcmp(command, "pause"), stop = !strcmp(command, "stop");
    bool seek = !strcmp(command, "seek"), speed = !strcmp(command, "speed"), volume = !strcmp(command, "volume");
    bool mute = !strcmp(command, "mute"), solo = !strcmp(command, "solo");
    if (!(play || pause || stop || seek || speed || volume || mute || solo) ||
        (seek && (value < 0 || value > p->duration)) || (speed && (value < 0.25 || value > 4)) ||
        (volume && (value < 0 || value > 1)) || ((mute || solo) && (value < 0 || value > 65535 || value != floor(value)))) return false;
    // the caller excludes the audio thread (audio_voice_lock) before control-thread mutations.
    MidiState previous = midi_state(p);
    double position = midi_position(p);
    bool audible[16];
    for (unsigned ch = 0; ch < 16; ch++) audible[ch] = midi_audible(p, ch);
    if (speed) { p->speed = value; p->anchor = position; p->elapsed_frames = 0; }
    if (volume) p->volume = value;
    if (mute) p->mute = (uint16_t)value;
    if (solo) p->solo = (uint16_t)value;
    bool running = play || (previous == MIDI_PLAYING && !pause && !stop);
    if (stop || (play && previous == MIDI_ENDED)) position = 0;
    if (seek) position = value;
    if (stop || seek || (play && previous != MIDI_PLAYING)) midi_restore(p, position, running);
    else if (pause) midi_silence(p);
    else if ((mute || solo) && running) for (unsigned ch = 0; ch < 16; ch++) {
        if (audible[ch] && !midi_audible(p, ch)) midi_silence(p, ch);
        if (!audible[ch] && midi_audible(p, ch)) midi_restore_notes(p, ch);
    }
    if (midi_state(p) == MIDI_ERROR) return false;
    midi_set_state(p, running ? MIDI_PLAYING : stop ? MIDI_READY : MIDI_PAUSED);
    return true;
}
