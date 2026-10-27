// fresh Bowyer-Watson cavity insertion with ghost hull faces (Shewchuk, 1996).
// Birth order preserves every reference triangle/edge ordering (PG7.3).
#include "delaunay.h"
#include "../memtrack.h"
#include "../math_utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#pragma STDC FP_CONTRACT OFF

typedef struct DelTriangle { int v[3], adj[3]; uint64_t birth; int seen, bad; bool live; } DelTriangle;
typedef struct DelOrder { uint64_t birth; int slot; } DelOrder;
typedef struct DelBoundary { int a, b, neighbour, slot, side; } DelBoundary;
typedef struct DelState {
    const double* points;
    DelTriangle* triangles;
    int* free_slots;
    int free_count, capacity, epoch;
    uint64_t birth;
    ProcgenExactPredicate exact;
    bool ok;
} DelState;

static int del_sign(DelState* s, int a, int b, int c, int d) {
    int ids[] = {a, b, c, d}, count = d < 0 ? 3 : 4, sign = 0;
    double p[8];
    for (int i = 0; i < count; i++) memcpy(p + i * 2, s->points + ids[i] * 2, 2 * sizeof(double));
    if (!procgen_predicate_filter(p, count, 2, count == 4, &sign) &&
            !s->exact(p, count, 2, count == 4, &sign)) s->ok = false;
    return sign;
}

static bool del_conflict(DelState* s, int index, int p) {
    int* v = s->triangles[index].v;
    if (v[2] >= 0) return del_sign(s, v[0], v[1], v[2], p) > 0;
    int o = del_sign(s, v[0], v[1], p, -1);
    if (o) return o > 0;
    const double *a = s->points + v[0] * 2, *b = s->points + v[1] * 2, *q = s->points + p * 2;
    return q[0] >= fmin(a[0], b[0]) && q[0] <= fmax(a[0], b[0]) &&
        q[1] >= fmin(a[1], b[1]) && q[1] <= fmax(a[1], b[1]);
}

static int del_create(DelState* s, int a, int b, int c) {
    if (!s->free_count) { s->ok = false; return -1; }
    int index = s->free_slots[--s->free_count];
    s->triangles[index] = (DelTriangle){{a, b, c}, {-1, -1, -1}, s->birth++, 0, 0, true};
    return index;
}

static int del_order_compare(const void* left, const void* right) {
    uint64_t a = ((const DelOrder*)left)->birth, b = ((const DelOrder*)right)->birth;
    return (a > b) - (a < b);
}

// Straight-line orientation walking locates a conflicting face. The bounded
// exhaustive search also handles an outside collinear extension of a hull edge.
static int del_locate(DelState* s, int p, int hint) {
    int index = hint;
    for (int step = 0; s->ok && step < s->capacity; step++) {
        DelTriangle* t = &s->triangles[index];
        if (t->v[2] < 0) {
            if (del_conflict(s, index, p)) return index;
            index = t->adj[0];
        } else {
            int side = -1;
            for (int j = 0; j < 3; j++) if (del_sign(s, t->v[j], t->v[(j + 1) % 3], p, -1) < 0) { side = j; break; }
            if (side < 0) return index;
            index = t->adj[side];
        }
        if (index < 0) { s->ok = false; break; }
    }
    for (int i = 0; s->ok && i < s->capacity; i++)
        if (s->triangles[i].live && del_conflict(s, i, p)) return i;
    return -1;
}

// Pair new radial edges by their old endpoint, including the ghost vertex -1.
static bool del_link_radial(DelState* s, DelBoundary* boundary, int count, int p, int* links) {
    for (int j = 0; j < count; j++) {
        DelTriangle* t = &s->triangles[boundary[j].slot];
        for (int side = 0; side < 3; side++) if (side != boundary[j].side) {
            int a = t->v[side], b = t->v[(side + 1) % 3];
            links[(a == p ? b + 1 : a + 1) * 2 + (a == p)] = boundary[j].slot * 3 + side;
        }
    }
    for (int j = 0; j < count; j++) {
        DelTriangle* t = &s->triangles[boundary[j].slot];
        for (int side = 0; side < 3; side++) if (side != boundary[j].side) {
            int a = t->v[side], b = t->v[(side + 1) % 3];
            int opposite = links[(a == p ? b + 1 : a + 1) * 2 + (a != p)];
            DelTriangle* other = &s->triangles[opposite / 3];
            if (other->v[opposite % 3] != b || other->v[(opposite + 1) % 3] != a) return false;
            t->adj[side] = opposite / 3;
        }
    }
    return true;
}

bool procgen_delaunay(const double* points, int count, const int* ids, int used,
        const int seed[3], ProcgenExactPredicate exact, int* output, int* triangles) {
    DelState s = {.points = points, .capacity = 2 * count + 4, .exact = exact, .ok = true};
    size_t cap = (size_t)s.capacity;
    s.triangles = mem_calloc(cap, sizeof(DelTriangle), MEM_CAT_TEMP);
    s.free_slots = mem_alloc(cap * sizeof(int), MEM_CAT_TEMP);
    DelOrder* order = mem_alloc(cap * sizeof(DelOrder), MEM_CAT_TEMP);
    DelBoundary* boundary = mem_alloc(cap * 3 * sizeof(DelBoundary), MEM_CAT_TEMP);
    int* queue = mem_alloc(cap * sizeof(int), MEM_CAT_TEMP);
    int* links = mem_calloc((size_t)(count + 1) * 2, sizeof(int), MEM_CAT_TEMP);
    s.ok = s.triangles && s.free_slots && order && boundary && queue && links;
    if (s.ok) {
        for (int i = 0; i < s.capacity; i++) s.free_slots[s.free_count++] = s.capacity - 1 - i;
        int a = seed[0], b = seed[1], c = seed[2];
        del_create(&s, a, b, c); del_create(&s, b, a, -1); del_create(&s, c, b, -1); del_create(&s, a, c, -1);
        // The seed has four faces: finite edges pair with ghosts; virtual edges pair around the hull.
        for (int i = 0; i < 4; i++) for (int e = 0; e < 3; e++)
            for (int j = 0; j < 4; j++) for (int f = 0; f < 3; f++)
                if (s.triangles[i].v[e] == s.triangles[j].v[(f + 1) % 3] &&
                    s.triangles[i].v[(e + 1) % 3] == s.triangles[j].v[f]) s.triangles[i].adj[e] = j;
        int hint = 0;
        for (int at = 0; s.ok && at < used; at++) {
            int p = ids[at];
            if (p == a || p == b || p == c) continue;
            s.epoch++;
            int first = del_locate(&s, p, hint), head = 0, tail = 0, bad = 0, edges = 0;
            if (first < 0) { s.ok = false; break; }
            queue[tail++] = first;
            s.triangles[first].seen = s.epoch;
            while (s.ok && head < tail) {
                int index = queue[head++];
                DelTriangle* t = &s.triangles[index];
                if (!del_conflict(&s, index, p)) continue;
                t->bad = s.epoch;
                order[bad++] = (DelOrder){t->birth, index};
                for (int j = 0; j < 3; j++) {
                    int next = t->adj[j];
                    if (next < 0) { s.ok = false; break; }
                    if (s.triangles[next].seen != s.epoch) {
                        s.triangles[next].seen = s.epoch;
                        queue[tail++] = next;
                    }
                }
            }
            if (!s.ok) break;
            qsort(order, (size_t)bad, sizeof(DelOrder), del_order_compare);
            for (int j = 0; j < bad; j++) {
                DelTriangle* t = &s.triangles[order[j].slot];
                for (int e = 0; e < 3; e++) if (s.triangles[t->adj[e]].bad != s.epoch)
                    boundary[edges++] = (DelBoundary){t->v[e], t->v[(e + 1) % 3], t->adj[e], -1, -1};
            }
            for (int j = 0; j < bad; j++) {
                s.triangles[order[j].slot].live = false;
                s.free_slots[s.free_count++] = order[j].slot;
            }
            for (int j = 0; s.ok && j < edges; j++) {
                DelBoundary* e = &boundary[j];
                e->slot = e->a < 0 ? del_create(&s, e->b, p, -1) :
                    e->b < 0 ? del_create(&s, p, e->a, -1) : del_create(&s, e->a, e->b, p);
                e->side = e->a < 0 ? 2 : e->b < 0 ? 1 : 0;
                if (!s.ok) break;
                s.triangles[e->slot].adj[e->side] = e->neighbour;
                DelTriangle* neighbour = &s.triangles[e->neighbour];
                for (int f = 0; f < 3; f++) if (neighbour->v[f] == e->b && neighbour->v[(f + 1) % 3] == e->a)
                    neighbour->adj[f] = e->slot;
            }
            if (s.ok && edges) s.ok = del_link_radial(&s, boundary, edges, p, links);
            if (!edges) s.ok = false;
            hint = edges ? boundary[0].slot : 0;
        }
        int live = 0;
        for (int i = 0; s.ok && i < s.capacity; i++) if (s.triangles[i].live)
            order[live++] = (DelOrder){s.triangles[i].birth, i};
        qsort(order, (size_t)live, sizeof(DelOrder), del_order_compare);
        for (int i = 0; i < live; i++) memcpy(output + i * 3, s.triangles[order[i].slot].v, 3 * sizeof(int));
        *triangles = live;
    }
    mem_free(links); mem_free(queue); mem_free(boundary); mem_free(order);
    mem_free(s.free_slots); mem_free(s.triangles);
    return s.ok;
}
