// fresh Beyer-style droplet kernel matching terrain.ls's snapshot batches (PG7.3).
#include "erosion.h"
#include "rng.h"
#include "../memtrack.h"
#include <string.h>
#pragma STDC FP_CONTRACT OFF

typedef struct ErosionPoint { int cell; double u, v, value, dx, dy; } ErosionPoint;

static ErosionPoint erosion_surface(const double* values, int width, double x, double y) {
    int ix = (int)floor(x), iy = (int)floor(y), c = iy * width + ix;
    double u = x - (double)ix, v = y - (double)iy;
    double a = values[c], b = values[c + 1], d = values[c + width], e = values[c + width + 1];
    return (ErosionPoint){c, u, v,
        a * (1.0 - u) * (1.0 - v) + b * u * (1.0 - v) + d * (1.0 - u) * v + e * u * v,
        (b - a) * (1.0 - v) + (e - d) * v, (d - a) * (1.0 - u) + (e - b) * u};
}

static void erosion_spread(const ErosionPoint* p, int width, double change, double* delta, uint8_t* seen) {
    int cells[] = {p->cell, p->cell + 1, p->cell + width, p->cell + width + 1};
    double u = p->u, v = p->v;
    double changes[] = {change * (1.0 - u) * (1.0 - v), change * u * (1.0 - v),
        change * (1.0 - u) * v, change * u * v};
    for (int j = 0; j < 4; j++) {
        int c = cells[j];
        // sum starts at the first member, not +0: retain signed zero and order.
        delta[c] = seen[c] ? delta[c] + changes[j] : changes[j];
        seen[c] = 1;
    }
}

static bool erosion_droplet(const double* values, int w, int h, const ProcgenErosion* s,
        uint64_t key, double* delta, uint8_t* seen) {
    double x = math_splitmix64_unit(procgen_key_at(key, 0)) * (double)(w - 1);
    double y = math_splitmix64_unit(procgen_key_at(key, 1)) * (double)(h - 1);
    double dx = 0.0, dy = 0.0, speed = 1.0, water = 1.0, sediment = 0.0;
    for (int step = 0; step < s->steps; step++) {
        ErosionPoint here = erosion_surface(values, w, x, y);
        double ndx = dx * s->inertia - here.dx * (1.0 - s->inertia);
        double ndy = dy * s->inertia - here.dy * (1.0 - s->inertia);
        double length = sqrt(ndx * ndx + ndy * ndy);
        if (!isfinite(length)) return false;
        if (length == 0.0) break;
        dx = ndx / length; dy = ndy / length;
        double nx = x + dx, ny = y + dy;
        if (nx < 0.0 || ny < 0.0 || nx >= (double)(w - 1) || ny >= (double)(h - 1)) break;
        double fall = erosion_surface(values, w, nx, ny).value - here.value;
        double capacity = math_max_double(0.0 - fall * speed * water * s->capacity, s->min_capacity);
        double change = sediment > capacity || fall > 0.0
            ? (fall > 0.0 ? math_min_double(fall, sediment) : (sediment - capacity) * s->deposit)
            : 0.0 - math_min_double((capacity - sediment) * s->erode, 0.0 - fall);
        if (!isfinite(change)) return false;
        erosion_spread(&here, w, change, delta, seen);
        speed = sqrt(math_max_double(0.0, speed * speed - fall * s->gravity));
        water *= 1.0 - s->evaporate;
        sediment -= change;
        x = nx; y = ny;
    }
    return true;
}

bool procgen_hydraulic(uint64_t key, const double* input, int w, int h,
        int droplets, int batch, const ProcgenErosion* s, double* output) {
    size_t cells = (size_t)w * h, bytes = cells * sizeof(double);
    double* next = mem_alloc(bytes, MEM_CAT_TEMP);
    double* delta = mem_alloc(bytes, MEM_CAT_TEMP);
    uint8_t* seen = mem_alloc(cells, MEM_CAT_TEMP);
    bool ok = next && delta && seen;
    memcpy(output, input, bytes);
    for (int first = 0; ok && first < droplets; first += batch) {
        memset(seen, 0, cells);
        int stop = first + batch < droplets ? first + batch : droplets;
        for (int i = first; i < stop; i++) if (!erosion_droplet(output, w, h, s,
                procgen_key_at(key, (uint64_t)i), delta, seen)) { ok = false; break; }
        for (int c = 0; ok && c < w * h; c++) {
            double v = output[c], lo = v, hi = v;
            int neighbours[] = {c - 1, c + 1, c - w, c + w};
            for (int j = 0; j < 4; j++) {
                int n = neighbours[j];
                if (n < 0 || n >= w * h || (n / w != c / w && n % w != c % w)) continue;
                lo = math_min_double(lo, output[n]); hi = math_max_double(hi, output[n]);
            }
            // every clamp reads the previous batch, never a partially updated map.
            next[c] = seen[c] ? math_max_double(lo, math_min_double(hi, v + delta[c])) : v;
            if (!isfinite(next[c])) ok = false;
        }
        if (ok) memcpy(output, next, bytes);
    }
    mem_free(seen); mem_free(delta); mem_free(next);
    return ok;
}
