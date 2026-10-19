// audio_reverb.cpp — Freeverb, written from Jezar's public-domain design.
//
// Tunings are the original 44.1 kHz delay lengths, scaled to the bus rate.

#include "audio_reverb.hpp"
#include "../lib/mem.h"

#include <math.h>
#include <string.h>

enum { REVERB_COMBS = 8, REVERB_ALLPASSES = 4, REVERB_STEREO_SPREAD = 23 };
static const uint32_t COMB_TUNING[REVERB_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const uint32_t ALLPASS_TUNING[REVERB_ALLPASSES] = {556, 441, 341, 225};
static const float REVERB_INPUT_GAIN = 0.015f;   // Freeverb's fixed gain into the combs
static const float ALLPASS_FEEDBACK = 0.5f;
// the medium-room preset: room 0.6 and damping 0.45 on Freeverb's scales.
static const float REVERB_ROOM = 0.6f * 0.28f + 0.7f;
static const float REVERB_DAMP = 0.45f * 0.4f;

struct ReverbComb { float* buffer; uint32_t size, index; float store; };
struct ReverbAllpass { float* buffer; uint32_t size, index; };
struct AudioReverb {
    ReverbComb combs[2][REVERB_COMBS];
    ReverbAllpass allpasses[2][REVERB_ALLPASSES];
    float* memory;
};

// recursive filters decay into subnormals, which are slow on some CPUs.
static inline float flush_tiny(float x) { return fabsf(x) < 1e-20f ? 0.0f : x; }

static inline float comb_process(ReverbComb* c, float input) {
    float output = c->buffer[c->index];
    c->store = flush_tiny(output * (1.0f - REVERB_DAMP) + c->store * REVERB_DAMP);
    c->buffer[c->index] = input + c->store * REVERB_ROOM;
    if (++c->index == c->size) c->index = 0;
    return output;
}
static inline float allpass_process(ReverbAllpass* a, float input) {
    float delayed = a->buffer[a->index];
    a->buffer[a->index] = flush_tiny(input + delayed * ALLPASS_FEEDBACK);
    if (++a->index == a->size) a->index = 0;
    return delayed - input;
}

static uint32_t scaled_length(uint32_t tuning, uint32_t rate) {
    uint32_t length = (uint32_t)lround((double)tuning * rate / 44100.0);
    return length ? length : 1;
}

AudioReverb* audio_reverb_create(uint32_t rate) {
    if (!rate) return nullptr;
    size_t floats = 0;
    for (int ch = 0; ch < 2; ch++) {
        uint32_t spread = ch ? REVERB_STEREO_SPREAD : 0;
        for (uint32_t t : COMB_TUNING) floats += scaled_length(t + spread, rate);
        for (uint32_t t : ALLPASS_TUNING) floats += scaled_length(t + spread, rate);
    }
    auto* r = (AudioReverb*)mem_calloc(1, sizeof(AudioReverb), MEM_CAT_RENDER);
    float* memory = r ? (float*)mem_calloc(floats, sizeof(float), MEM_CAT_RENDER) : nullptr;
    if (!memory) { mem_free(r); return nullptr; }
    r->memory = memory;
    for (int ch = 0; ch < 2; ch++) {
        uint32_t spread = ch ? REVERB_STEREO_SPREAD : 0;
        for (int i = 0; i < REVERB_COMBS; i++) {
            r->combs[ch][i] = {memory, scaled_length(COMB_TUNING[i] + spread, rate), 0, 0};
            memory += r->combs[ch][i].size;
        }
        for (int i = 0; i < REVERB_ALLPASSES; i++) {
            r->allpasses[ch][i] = {memory, scaled_length(ALLPASS_TUNING[i] + spread, rate), 0};
            memory += r->allpasses[ch][i].size;
        }
    }
    return r;
}

void audio_reverb_destroy(AudioReverb* r) {
    if (!r) return;
    mem_free(r->memory);
    mem_free(r);
}

void audio_reverb_process(AudioReverb* r, const float* input, float* output, uint32_t frames) {
    for (uint32_t f = 0; f < frames; f++) {
        // both channels feed one mono input; the offset right-channel delays make the stereo image.
        float in = (input[f * 2] + input[f * 2 + 1]) * REVERB_INPUT_GAIN;
        float left = 0, right = 0;
        for (int i = 0; i < REVERB_COMBS; i++) {
            left += comb_process(&r->combs[0][i], in);
            right += comb_process(&r->combs[1][i], in);
        }
        for (int i = 0; i < REVERB_ALLPASSES; i++) {
            left = allpass_process(&r->allpasses[0][i], left);
            right = allpass_process(&r->allpasses[1][i], right);
        }
        // full width: each output channel carries its own tail (Freeverb wet1 = 1, wet2 = 0).
        output[f * 2] += flush_tiny(left);
        output[f * 2 + 1] += flush_tiny(right);
    }
}
