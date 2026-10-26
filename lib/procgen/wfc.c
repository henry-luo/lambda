// Fresh counting propagator after Gumin's WaveFunctionCollapse (2016).
// The Lambda reference defines ordering and keyed choices (PG2, PG7.3).
#include "wfc.h"
#include "rng.h"
#include "../memtrack.h"
#include <stddef.h>
#include <string.h>

// also covers the monolithic CLI build, which compiles lib sources directly.
#pragma STDC FP_CONTRACT OFF

typedef struct WfcState {
    int width, height, patterns, cells;
    bool periodic;
    const uint8_t* masks;
    uint8_t* domains;
    uint16_t* counts;
    int* sizes;
    int* queue;
    int head, tail;
} WfcState;

static int wfc_neighbour(const WfcState* s, int cell, int direction) {
    static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    int x = cell % s->width + dx[direction], y = cell / s->width + dy[direction];
    if (s->periodic) return ((y + s->height) % s->height) * s->width + (x + s->width) % s->width;
    return x < 0 || y < 0 || x >= s->width || y >= s->height ? -1 : y * s->width + x;
}

static bool wfc_ban(WfcState* s, int cell, int pattern) {
    int index = cell * s->patterns + pattern;
    if (!s->domains[index]) return true;
    s->domains[index] = 0;
    // each cell/pattern is removed once per attempt, bounding the event queue.
    s->queue[s->tail++] = index;
    return --s->sizes[cell] > 0;
}

static bool wfc_propagate(WfcState* s) {
    int t = s->patterns;
    while (s->head < s->tail) {
        int event = s->queue[s->head++], cell = event / t, removed = event % t;
        for (int d = 0; d < 4; d++) {
            int next = wfc_neighbour(s, cell, d);
            if (next < 0) continue;
            uint16_t* counts = s->counts + (cell * 4 + d) * t;
            const uint8_t* mask = s->masks + (d * t + removed) * t;
            for (int p = 0; p < t; p++) {
                if (mask[p] && --counts[p] == 0 && !wfc_ban(s, next, p)) return false;
            }
        }
    }
    return true;
}

static bool wfc_attempt(WfcState* s, uint64_t key, const double* weights,
        const uint16_t* full, int* output) {
    int t = s->patterns;
    memset(s->domains, 1, (size_t)s->cells * t);
    s->head = s->tail = 0;
    for (int c = 0; c < s->cells; c++) {
        s->sizes[c] = t;
        memcpy(s->counts + c * 4 * t, full, (size_t)4 * t * sizeof(uint16_t));
    }
    // full domains can already lack support, including at periodic self-neighbours.
    for (int c = 0; c < s->cells; c++) for (int d = 0; d < 4; d++) {
        int next = wfc_neighbour(s, c, d);
        if (next < 0) continue;
        for (int p = 0; p < t; p++) if (!full[d * t + p] && !wfc_ban(s, next, p)) return false;
    }
    if (!wfc_propagate(s)) return false;
    for (int step = 0; step <= s->cells; step++) {
        int least = t + 1, ties = 0;
        for (int c = 0; c < s->cells; c++) {
            int size = s->sizes[c];
            if (size > 1 && size < least) { least = size; ties = 1; }
            else if (size == least) ties++;
        }
        if (!ties) {
            for (int c = 0; c < s->cells; c++) for (int p = 0; p < t; p++) {
                if (s->domains[c * t + p]) { output[c] = p; break; }
            }
            return true;
        }
        uint64_t draw = procgen_key_at(key, (uint64_t)step);
        int rank = (int)(math_splitmix64_unit(procgen_key_at(draw, 0)) * (double)ties), cell = 0;
        for (; cell < s->cells; cell++) if (s->sizes[cell] == least && rank-- == 0) break;
        uint8_t* domain = s->domains + cell * t;
        double total = 0.0;
        for (int p = 0; p < t; p++) if (domain[p]) total += weights[p];
        double target = math_splitmix64_unit(procgen_key_at(draw, 1)) * total, cumulative = 0.0;
        int chosen = -1;
        // ascending pattern order and sequential sums match rng.weighted's cumsum.
        for (int p = 0; p < t; p++) if (domain[p]) {
            cumulative += weights[p];
            if (cumulative > target) { chosen = p; break; }
        }
        if (chosen < 0) return false;
        for (int p = 0; p < t; p++) if (p != chosen && !wfc_ban(s, cell, p)) return false;
        if (!wfc_propagate(s)) return false;
    }
    return false;
}

ProcgenWfcResult procgen_wfc(uint64_t key, int width, int height, bool periodic,
        int64_t retries, int patterns, const double* weights, const uint8_t* masks, int* output) {
    WfcState s = {.width = width, .height = height, .patterns = patterns,
        .cells = width * height, .periodic = periodic, .masks = masks};
    size_t slots = (size_t)s.cells * patterns;
    s.domains = mem_alloc(slots, MEM_CAT_TEMP);
    s.counts = mem_alloc(slots * 4 * sizeof(uint16_t), MEM_CAT_TEMP);
    s.sizes = mem_alloc((size_t)s.cells * sizeof(int), MEM_CAT_TEMP);
    s.queue = mem_alloc(slots * sizeof(int), MEM_CAT_TEMP);
    ProcgenWfcResult result = PROCGEN_WFC_NO_MEMORY;
    if (s.domains && s.counts && s.sizes && s.queue) {
        uint16_t full[4 * 1000] = {0};
        for (int d = 0; d < 4; d++) for (int p = 0; p < patterns; p++)
            for (int q = 0; q < patterns; q++) full[d * patterns + q] += masks[(d * patterns + p) * patterns + q];
        result = PROCGEN_WFC_CONTRADICTION;
        for (int64_t i = 0; i < retries; i++) {
            if (wfc_attempt(&s, procgen_key_at(key, (uint64_t)i), weights, full, output)) {
                result = PROCGEN_WFC_OK;
                break;
            }
        }
    }
    mem_free(s.queue);
    mem_free(s.sizes);
    mem_free(s.counts);
    mem_free(s.domains);
    return result;
}
