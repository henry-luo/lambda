#pragma once
#include "../radiant/audio.hpp"
#include "../radiant/speech.hpp"
#include "../radiant/rdt_video.h"
#include "../lib/file.h"
#include "../lib/mem.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>

static inline bool audio_fixture_video_wait(RdtVideo* video, RdtVideoState target, unsigned timeout_ms) {
    uint64_t start = uv_hrtime();
    do {
        rdt_video_pump_events();
        if (rdt_video_get_state(video) == target) return true;
        uv_sleep(10);
    } while ((uv_hrtime() - start) / 1000000 < timeout_ms);
    return rdt_video_get_state(video) == target;
}

static inline bool audio_fixture_graph_publish(AudioSubmixId submix, AudioGraphId graph, const AudioGraphConfig& config) {
    AudioGraph* prepared = nullptr; const char* error = nullptr;
    if (!audio_submix_graph_prepare(submix, graph, config, &prepared, &error)) return false;
    if (audio_submix_graph_commit(submix, graph, prepared, nullptr, 0)) return true;
    audio_graph_destroy(prepared); return false;
}

static inline void audio_fixture_le(unsigned char* out, uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) out[i] = (unsigned char)(value >> (i * 8));
}

// independent PCM16 input keeps decoder/mixer assertions free of an encoder dependency.
static inline size_t audio_fixture_wave(unsigned char* out, size_t capacity, uint32_t rate,
    uint16_t channels, uint32_t frames, int16_t left, int16_t right) {
    size_t size = 44 + (size_t)frames * channels * 2;
    if (!out || !channels || channels > 2 || size > capacity || size - 8 > UINT32_MAX) return 0;
    memset(out, 0, size);
    memcpy(out, "RIFF", 4); audio_fixture_le(out + 4, (uint32_t)(size - 8), 4);
    memcpy(out + 8, "WAVEfmt ", 8); audio_fixture_le(out + 16, 16, 4);
    audio_fixture_le(out + 20, 1, 2); audio_fixture_le(out + 22, channels, 2);
    audio_fixture_le(out + 24, rate, 4); audio_fixture_le(out + 28, rate * channels * 2, 4);
    audio_fixture_le(out + 32, channels * 2, 2); audio_fixture_le(out + 34, 16, 2);
    memcpy(out + 36, "data", 4); audio_fixture_le(out + 40, (uint32_t)(size - 44), 4);
    for (uint32_t i = 0; i < frames; i++)
        for (uint16_t c = 0; c < channels; c++)
            audio_fixture_le(out + 44 + ((size_t)i * channels + c) * 2, (uint16_t)(c ? right : left), 2);
    return size;
}

static inline uint64_t audio_fixture_midi(DomElement* owner, double duration = .01) {
    char* bytes = nullptr; size_t length = 0;
    if (!file_read_all("lmd/package/music/soundfonts/TimGM6mb.sf2", MEM_CAT_RENDER, &bytes, &length)) return 0;
    MidiEvent events[] = {{0, 144, 60, 100}, {duration * .5, 128, 60, 0}};
    uint64_t token = radiant_midi_open(owner, events, 2, duration, bytes, length); mem_free(bytes); return token;
}

struct AudioFixtureSpeechEngine {
    char* previous = nullptr;
    bool changed = false;
    bool use_fake() {
        if (changed) return true;
        const char* value = getenv("LAMBDA_SPEECH_ENGINE");
        previous = value ? mem_strdup(value, MEM_CAT_RENDER) : nullptr;
        if (value && !previous) return false;
#ifdef _WIN32
        _putenv_s("LAMBDA_SPEECH_ENGINE", "fake");
#else
        setenv("LAMBDA_SPEECH_ENGINE", "fake", 1);
#endif
        changed = true; return true;
    }
    ~AudioFixtureSpeechEngine() {
        if (changed) {
#ifdef _WIN32
            _putenv_s("LAMBDA_SPEECH_ENGINE", previous ? previous : "");
#else
            if (previous) setenv("LAMBDA_SPEECH_ENGINE", previous, 1); else unsetenv("LAMBDA_SPEECH_ENGINE");
#endif
        }
        mem_free(previous);
    }
};
static inline uint64_t audio_fixture_speech(DomElement* owner, const char* words = "go", float gain = 1, double at = -1) {
    SpeechText text = {}; if (!speech_text_from_string(words, strlen(words), &text)) { speech_text_release(&text); return 0; }
    SpeechOptions options = {nullptr, nullptr, 1, 1, gain, at}; const char* error = nullptr;
    return radiant_speech_speak(owner, &text, &options, &error);
}
