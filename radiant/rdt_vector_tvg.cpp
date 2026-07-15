// rdt_vector_tvg.cpp — ThorVG backend for RdtVector
// All ThorVG C API calls are isolated in this file.
// Radiant rendering code calls only rdt_* functions.

#include "render.hpp"
#include "../lib/atomic.h"
#include "../lib/log.h"
#include "../lib/lambda_alloca.h"
#include "../lib/mem_grow.hpp"
#include "../lib/mem_factory.h"
#include "../lib/hashmap.h"
#include <thorvg_capi.h>
#include "../lib/mem.h"
#include "../lib/mempool.h"
#include "../lib/url.h"
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/lambda-data.hpp"
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// Internal types
// ============================================================================

struct RdtVectorImpl {
    Tvg_Canvas canvas;
    Tvg_Paint batch_scene;
    uint32_t* pixels;
    int width;
    int height;
    int stride;
    int batch_depth;
    float tile_offset_x;  // physical-pixel X start of current tile (0 = full page)
    float tile_offset_y;  // physical-pixel Y start of current tile (0 = full page)
};

// Path stores ThorVG path commands for deferred replay
struct RdtPath {
    // store raw path commands; replay into Tvg_Paint at draw time
    enum Cmd { CMD_MOVE, CMD_LINE, CMD_CUBIC, CMD_CLOSE, CMD_RECT, CMD_CIRCLE };
    struct Entry {
        Cmd cmd;
        float args[8]; // max args: cubic(6), rect(6), circle(4)
    };
    Entry* entries;
    int count;
    int capacity;
};

struct RdtPicture {
    // Two kinds of pictures:
    //  - SVG_DOM: Radiant-parsed SVG DOM (Element*) drawn via DisplayList replay.
    //    Used by rdt_picture_load* for file/data SVG.  Goes through the same
    //    code path as inline <svg> in HTML, so font weight/style/family
    //    resolution is uniform with HTML body text.
    //  - TVG_PAINT: a Tvg_Paint owned by ThorVG (text or image), wrapped via
    //    rdt_picture_take_tvg_paint and used internally by render_svg_inline
    //    for SVG <text>/<image> primitives.
    enum Kind { KIND_TVG_PAINT, KIND_SVG_DOM };
    Kind kind;

    // KIND_TVG_PAINT
    Tvg_Paint paint;

    // KIND_SVG_DOM
    Input* input;          // owns the parse arena (allocated from owned pool)
    Pool* pool;            // owned pool (created by rdt_picture_load*); freed on rdt_picture_free
    bool owns_pool;        // true for originals, false for dups
    atomic_int32 ref_count; // SVG_DOM owner references, including cache and duplicate handles
    RdtPicture* owner;     // duplicate handles retain the owning SVG_DOM picture
    Element* svg_root;     // root <svg> element
    char* source_path;     // original file path for resolving nested SVG refs

    // Common
    float width;           // intrinsic width (from viewBox or width attr) or override via set_size
    float height;
    RdtMatrix transform;   // optional explicit transform (set via rdt_picture_set_transform)
    bool has_transform;

    RdtPicture* dup();
    void release();
};

// Process-wide font context for SVG-DOM pictures (set by ui_context).
// Inline <svg> uses RenderContext->ui_context->font_ctx; standalone pictures
// rasterized off-screen by media helpers do not have a render context, so
// we keep a global pointer set once at startup.
static FontContext* g_picture_font_ctx = nullptr;

// SVG_DOM pictures are immutable after parsing. Keep parsed external SVGs and
// repeated SVG data payloads alive for the process lifetime so callers can draw
// cheap shallow duplicates instead of reparsing and rebuilding SVG resources.
typedef struct RdtPictureCacheEntry {
    char* path_key;
    uint64_t data_hash;
    int data_size;
    char* mime_key;
    char* data_copy;
    RdtPicture* picture;
    uint64_t last_used;
} RdtPictureCacheEntry;

static HashMap* g_picture_path_cache = nullptr;
static HashMap* g_picture_data_cache = nullptr;
static uint64_t g_picture_cache_clock = 0;
static const size_t RDT_PICTURE_CACHE_MAX_ENTRIES = 64;
static pthread_mutex_t g_picture_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum RdtPaintCacheKind {
    RDT_PAINT_CACHE_FILL_PATH = 1,
    RDT_PAINT_CACHE_STROKE_PATH,
    RDT_PAINT_CACHE_LINEAR_GRADIENT,
    RDT_PAINT_CACHE_RADIAL_GRADIENT,
} RdtPaintCacheKind;

typedef struct RdtPaintCacheEntry {
    RdtPaintCacheKind kind;
    uint64_t hash;
    RdtPath* path;
    Color color;
    RdtFillRule fill_rule;
    RdtStrokeCap stroke_cap;
    RdtStrokeJoin stroke_join;
    float stroke_width;
    float dash_phase;
    float* dash_array;
    int dash_count;
    float gradient_values[5];
    RdtGradientStop* stops;
    int stop_count;
    Tvg_Paint paint;
} RdtPaintCacheEntry;

static HashMap* g_paint_cache = nullptr;
static int g_paint_cache_count = 0;
static const int RDT_PAINT_CACHE_MAX_ENTRIES = 256;
static pthread_mutex_t g_paint_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct RdtImagePaintCacheEntry {
    const uint32_t* pixels;
    uint64_t generation;
    int src_w;
    int src_h;
    int src_stride;
    Tvg_Paint paint;
} RdtImagePaintCacheEntry;

static HashMap* g_image_paint_cache = nullptr;
static int g_image_paint_cache_count = 0;
static const int RDT_IMAGE_PAINT_CACHE_MAX_ENTRIES = 128;
static pthread_mutex_t g_image_paint_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct RdtVectorCacheOwner {
    MemContext* context;
    MemNode* registry_node;
} RdtVectorCacheOwner;

static RdtVectorCacheOwner g_vector_cache_owner = {};

static void picture_cache_clear_all();
static void paint_cache_clear_all();
static void image_paint_cache_clear_all();

static bool vector_cache_stat(void* allocator, MemStatSample* sample) {
    (void)allocator;
    if (!sample) return false;
    pthread_mutex_lock(&g_picture_cache_mutex);
    size_t picture_count = (g_picture_path_cache ? hashmap_count(g_picture_path_cache) : 0) +
                           (g_picture_data_cache ? hashmap_count(g_picture_data_cache) : 0);
    pthread_mutex_unlock(&g_picture_cache_mutex);
    pthread_mutex_lock(&g_paint_cache_mutex);
    size_t paint_count = g_paint_cache ? hashmap_count(g_paint_cache) : 0;
    pthread_mutex_unlock(&g_paint_cache_mutex);
    pthread_mutex_lock(&g_image_paint_cache_mutex);
    size_t image_count = g_image_paint_cache ? hashmap_count(g_image_paint_cache) : 0;
    pthread_mutex_unlock(&g_image_paint_cache_mutex);
    sample->alloc_count = picture_count + paint_count + image_count;
    sample->bytes_in_use = picture_count * sizeof(RdtPictureCacheEntry) +
                           paint_count * sizeof(RdtPaintCacheEntry) +
                           image_count * sizeof(RdtImagePaintCacheEntry);
    sample->bytes_reserved = sample->bytes_in_use;
    return true;
}

static void vector_cache_destroy(void* allocator) {
    (void)allocator;
    image_paint_cache_clear_all();
    paint_cache_clear_all();
    picture_cache_clear_all();
}

// Mutex to serialize tvg_paint_duplicate calls (ThorVG Picture::duplicate is not thread-safe:
// it increments a shared non-atomic counter and may mutate the source loader's state).
static pthread_mutex_t g_tvg_dup_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// Helpers
// ============================================================================

static bool path_ensure_capacity(RdtPath* p) {
    if (!p) return false;
    if (p->count < p->capacity) return true;
    int old_capacity = p->capacity;
    if (lam::mem_grow_array(&p->entries, &p->capacity, p->count + 1, 16,
                            MEM_CAT_RENDER)) {
        return true;
    }
    // path entries are appended immediately after growth; failing closed prevents writing past the old buffer.
    log_error("RAD_CAP_RDT_PATH: unable to grow path entries from %d to %d",
              old_capacity, p->count + 1);
    return false;
}

static RdtPath::Entry* path_append_entry(RdtPath* path, RdtPath::Cmd command) {
    if (!path_ensure_capacity(path)) return nullptr;
    RdtPath::Entry* entry = &path->entries[path->count++];
    entry->cmd = command;
    return entry;
}

static uint64_t rdt_picture_data_hash(const char* data, int size, const char* mime_type) {
    uint64_t h = 1469598103934665603ULL;
    const unsigned char* bytes = (const unsigned char*)data;
    for (int i = 0; i < size; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    if (mime_type) {
        const unsigned char* mime = (const unsigned char*)mime_type;
        while (*mime) {
            h ^= *mime++;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static void rdt_hash_bytes(uint64_t* h, const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; i++) {
        *h ^= bytes[i];
        *h *= 1099511628211ULL;
    }
}

static uint64_t rdt_paint_hash_path(const RdtPath* path) {
    uint64_t h = 1469598103934665603ULL;
    if (!path) return h;
    rdt_hash_bytes(&h, &path->count, sizeof(path->count));
    for (int i = 0; i < path->count; i++) {
        const RdtPath::Entry* e = &path->entries[i];
        rdt_hash_bytes(&h, &e->cmd, sizeof(e->cmd));
        rdt_hash_bytes(&h, e->args, sizeof(e->args));
    }
    return h;
}

static uint64_t rdt_paint_hash_common(RdtPaintCacheKind kind, const RdtPath* path) {
    uint64_t h = rdt_paint_hash_path(path);
    rdt_hash_bytes(&h, &kind, sizeof(kind));
    return h;
}

static Tvg_Paint tvg_duplicate_paint_locked(Tvg_Paint paint) {
    if (!paint) return nullptr;
    pthread_mutex_lock(&g_tvg_dup_mutex);
    Tvg_Paint dup = tvg_paint_duplicate(paint);
    pthread_mutex_unlock(&g_tvg_dup_mutex);
    return dup;
}

static uint64_t picture_cache_next_stamp_locked() {
    g_picture_cache_clock++;
    if (g_picture_cache_clock == 0) g_picture_cache_clock++;
    return g_picture_cache_clock;
}

static RdtPicture* rdt_picture_svg_owner(RdtPicture* pic) {
    return pic && pic->owner ? pic->owner : pic;
}

static bool rdt_picture_retain_svg_owner(RdtPicture* pic) {
    RdtPicture* owner = rdt_picture_svg_owner(pic);
    if (!owner || owner->kind != RdtPicture::KIND_SVG_DOM) return false;
    atomic_inc32(&owner->ref_count);
    return true;
}

static void picture_cache_entry_release(const RdtPictureCacheEntry* entry) {
    if (!entry) return;
    if (entry->path_key) mem_free(entry->path_key);
    if (entry->mime_key) mem_free(entry->mime_key);
    if (entry->data_copy) mem_free(entry->data_copy);
    if (entry->picture) rdt_picture_free(entry->picture);
}

static RdtPicture* picture_cache_dup_path_locked(const char* path) {
    if (!g_picture_path_cache) return nullptr;
    RdtPictureCacheEntry query = {};
    query.path_key = (char*)path;
    const RdtPictureCacheEntry* e =
        (const RdtPictureCacheEntry*)hashmap_get(g_picture_path_cache, &query);
    if (e) {
        ((RdtPictureCacheEntry*)e)->last_used = picture_cache_next_stamp_locked();
        return rdt_picture_dup(e->picture);
    }
    return nullptr;
}

static bool picture_cache_data_matches(RdtPictureCacheEntry* e, const char* data,
                                       int size, const char* mime_type, uint64_t hash) {
    if (!e || e->data_hash != hash || e->data_size != size) return false;
    const char* entry_mime = e->mime_key ? e->mime_key : "";
    const char* query_mime = mime_type ? mime_type : "";
    if (strcmp(entry_mime, query_mime) != 0) return false;
    return e->data_copy && memcmp(e->data_copy, data, (size_t)size) == 0;
}

static uint64_t picture_path_cache_hash(const void* item, uint64_t s0, uint64_t s1) {
    const RdtPictureCacheEntry* e = (const RdtPictureCacheEntry*)item;
    const char* key = e->path_key ? e->path_key : "";
    return hashmap_sip(key, strlen(key), s0, s1);
}

static int picture_path_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const RdtPictureCacheEntry* ea = (const RdtPictureCacheEntry*)a;
    const RdtPictureCacheEntry* eb = (const RdtPictureCacheEntry*)b;
    const char* ka = ea->path_key ? ea->path_key : "";
    const char* kb = eb->path_key ? eb->path_key : "";
    return strcmp(ka, kb);
}

static uint64_t picture_data_cache_hash(const void* item, uint64_t s0, uint64_t s1) {
    const RdtPictureCacheEntry* e = (const RdtPictureCacheEntry*)item;
    uint64_t h = hashmap_sip(&e->data_hash, sizeof(e->data_hash), s0, s1);
    h ^= hashmap_sip(&e->data_size, sizeof(e->data_size), s0, s1);
    const char* mime = e->mime_key ? e->mime_key : "";
    h ^= hashmap_sip(mime, strlen(mime), s0, s1);
    return h;
}

static int picture_data_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const RdtPictureCacheEntry* ea = (const RdtPictureCacheEntry*)a;
    const RdtPictureCacheEntry* eb = (const RdtPictureCacheEntry*)b;
    if (ea->data_hash != eb->data_hash) return ea->data_hash < eb->data_hash ? -1 : 1;
    if (ea->data_size != eb->data_size) return ea->data_size < eb->data_size ? -1 : 1;
    const char* ma = ea->mime_key ? ea->mime_key : "";
    const char* mb = eb->mime_key ? eb->mime_key : "";
    int mime_cmp = strcmp(ma, mb);
    if (mime_cmp != 0) return mime_cmp;
    if (ea->data_size <= 0) return 0;
    if (!ea->data_copy || !eb->data_copy) return ea->data_copy == eb->data_copy ? 0 : (ea->data_copy ? 1 : -1);
    return memcmp(ea->data_copy, eb->data_copy, (size_t)ea->data_size);
}

static bool picture_cache_ensure_path_locked() {
    if (g_picture_path_cache) return true;
    g_picture_path_cache = hashmap_new(sizeof(RdtPictureCacheEntry), 32, 0, 0,
        picture_path_cache_hash, picture_path_cache_cmp, NULL, NULL);
    return g_picture_path_cache != nullptr;
}

static bool picture_cache_ensure_data_locked() {
    if (g_picture_data_cache) return true;
    g_picture_data_cache = hashmap_new(sizeof(RdtPictureCacheEntry), 32, 0, 0,
        picture_data_cache_hash, picture_data_cache_cmp, NULL, NULL);
    return g_picture_data_cache != nullptr;
}

static RdtPicture* picture_cache_dup_data_locked(const char* data, int size,
                                                 const char* mime_type, uint64_t hash) {
    if (!g_picture_data_cache) return nullptr;
    RdtPictureCacheEntry query = {};
    query.data_hash = hash;
    query.data_size = size;
    query.mime_key = (char*)(mime_type ? mime_type : "");
    query.data_copy = (char*)data;
    const RdtPictureCacheEntry* e =
        (const RdtPictureCacheEntry*)hashmap_get(g_picture_data_cache, &query);
    if (e && picture_cache_data_matches((RdtPictureCacheEntry*)e, data, size, mime_type, hash)) {
        ((RdtPictureCacheEntry*)e)->last_used = picture_cache_next_stamp_locked();
        return rdt_picture_dup(e->picture);
    }
    return nullptr;
}

typedef struct PictureCacheEvictionCandidate {
    RdtPictureCacheEntry entry;
    bool found;
} PictureCacheEvictionCandidate;

static bool picture_cache_find_lru_scan(const void* item, void* udata) {
    PictureCacheEvictionCandidate* candidate = (PictureCacheEvictionCandidate*)udata;
    const RdtPictureCacheEntry* entry = (const RdtPictureCacheEntry*)item;
    if (!candidate->found || entry->last_used < candidate->entry.last_used) {
        candidate->entry = *entry;
        candidate->found = true;
    }
    return true;
}

static void picture_cache_evict_to_capacity_locked(HashMap* cache) {
    if (!cache || RDT_PICTURE_CACHE_MAX_ENTRIES == 0) return;
    while (hashmap_count(cache) > RDT_PICTURE_CACHE_MAX_ENTRIES) {
        PictureCacheEvictionCandidate candidate = {};
        hashmap_scan(cache, picture_cache_find_lru_scan, &candidate);
        if (!candidate.found) return;
        const RdtPictureCacheEntry* removed =
            (const RdtPictureCacheEntry*)hashmap_delete(cache, &candidate.entry);
        if (!removed) return;
        // SVG handles can outlive cache eviction; refcounted owners keep the
        // parsed tree alive until the last display-list/media duplicate exits.
        picture_cache_entry_release(removed);
    }
}

static bool picture_cache_insert_path_locked(const char* path, RdtPicture* picture) {
    if (!path || !picture) return false;
    if (!picture_cache_ensure_path_locked()) return false;
    RdtPictureCacheEntry e = {};
    e.path_key = mem_strdup(path, MEM_CAT_CACHE_IMAGE);
    e.picture = picture;
    e.last_used = picture_cache_next_stamp_locked();
    if (!e.path_key) return false;
    const RdtPictureCacheEntry* replaced =
        (const RdtPictureCacheEntry*)hashmap_set(g_picture_path_cache, &e);
    if (hashmap_oom(g_picture_path_cache)) {
        if (e.path_key) mem_free(e.path_key);
        return false;
    }
    if (replaced) picture_cache_entry_release(replaced);
    picture_cache_evict_to_capacity_locked(g_picture_path_cache);
    return true;
}

static bool picture_cache_insert_data_locked(const char* data, int size,
                                             const char* mime_type, uint64_t hash,
                                             RdtPicture* picture) {
    if (!data || size <= 0 || !picture) return false;
    if (!picture_cache_ensure_data_locked()) return false;
    RdtPictureCacheEntry e = {};
    e.data_copy = (char*)mem_alloc((size_t)size, MEM_CAT_CACHE_IMAGE);
    if (!e.data_copy) {
        return false;
    }
    memcpy(e.data_copy, data, (size_t)size);
    e.data_size = size;
    e.data_hash = hash;
    e.mime_key = mem_strdup(mime_type ? mime_type : "", MEM_CAT_CACHE_IMAGE);
    e.picture = picture;
    e.last_used = picture_cache_next_stamp_locked();
    if (!e.mime_key) {
        mem_free(e.data_copy);
        return false;
    }
    const RdtPictureCacheEntry* replaced =
        (const RdtPictureCacheEntry*)hashmap_set(g_picture_data_cache, &e);
    if (hashmap_oom(g_picture_data_cache)) {
        mem_free(e.data_copy);
        mem_free(e.mime_key);
        return false;
    }
    if (replaced) picture_cache_entry_release(replaced);
    picture_cache_evict_to_capacity_locked(g_picture_data_cache);
    return true;
}

static bool picture_cache_free_scan(const void* item, void* udata) {
    (void)udata;
    const RdtPictureCacheEntry* entry = (const RdtPictureCacheEntry*)item;
    picture_cache_entry_release(entry);
    return true;
}

static void picture_cache_clear_all() {
    pthread_mutex_lock(&g_picture_cache_mutex);
    HashMap* path_cache = g_picture_path_cache;
    HashMap* data_cache = g_picture_data_cache;
    g_picture_path_cache = nullptr;
    g_picture_data_cache = nullptr;
    pthread_mutex_unlock(&g_picture_cache_mutex);

    if (path_cache) {
        hashmap_scan(path_cache, picture_cache_free_scan, NULL);
        hashmap_free(path_cache);
    }
    if (data_cache) {
        hashmap_scan(data_cache, picture_cache_free_scan, NULL);
        hashmap_free(data_cache);
    }
}

static bool paint_cache_path_equals(const RdtPath* a, const RdtPath* b) {
    if (a == b) return true;
    if (!a || !b || a->count != b->count) return false;
    if (a->count <= 0) return true;
    return memcmp(a->entries, b->entries, (size_t)a->count * sizeof(RdtPath::Entry)) == 0;
}

static bool paint_cache_color_equals(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static bool paint_cache_float_array_equals(const float* a, const float* b, int count) {
    if (count <= 0) return true;
    if (!a || !b) return false;
    return memcmp(a, b, (size_t)count * sizeof(float)) == 0;
}

static bool paint_cache_stops_equal(const RdtGradientStop* a, const RdtGradientStop* b, int count) {
    if (count <= 0) return true;
    if (!a || !b) return false;
    return memcmp(a, b, (size_t)count * sizeof(RdtGradientStop)) == 0;
}

static bool paint_cache_entry_matches_fill(RdtPaintCacheEntry* e, uint64_t hash,
                                           RdtPaintCacheKind kind, const RdtPath* path,
                                           Color color, RdtFillRule rule) {
    return e && e->hash == hash && e->kind == kind &&
        e->fill_rule == rule && paint_cache_color_equals(e->color, color) &&
        paint_cache_path_equals(e->path, path);
}

static bool paint_cache_entry_matches_stroke(RdtPaintCacheEntry* e, uint64_t hash,
                                             const RdtPath* path, Color color, float width,
                                             RdtStrokeCap cap, RdtStrokeJoin join,
                                             const float* dash_array, int dash_count,
                                             float dash_phase) {
    return e && e->hash == hash && e->kind == RDT_PAINT_CACHE_STROKE_PATH &&
        paint_cache_color_equals(e->color, color) && e->stroke_width == width &&
        e->stroke_cap == cap && e->stroke_join == join &&
        e->dash_count == dash_count && e->dash_phase == dash_phase &&
        paint_cache_float_array_equals(e->dash_array, dash_array, dash_count) &&
        paint_cache_path_equals(e->path, path);
}

static bool paint_cache_entry_matches_gradient(RdtPaintCacheEntry* e, uint64_t hash,
                                               RdtPaintCacheKind kind, const RdtPath* path,
                                               const float* values, const RdtGradientStop* stops,
                                               int stop_count, RdtFillRule rule) {
    int value_count = kind == RDT_PAINT_CACHE_LINEAR_GRADIENT ? 4 : 3;
    return e && e->hash == hash && e->kind == kind && e->fill_rule == rule &&
        e->stop_count == stop_count &&
        memcmp(e->gradient_values, values, (size_t)value_count * sizeof(float)) == 0 &&
        paint_cache_stops_equal(e->stops, stops, stop_count) &&
        paint_cache_path_equals(e->path, path);
}

static uint64_t paint_cache_hash_entry(const void* item, uint64_t s0, uint64_t s1) {
    const RdtPaintCacheEntry* e = (const RdtPaintCacheEntry*)item;
    uint64_t h = hashmap_sip(&e->hash, sizeof(e->hash), s0, s1);
    h ^= hashmap_sip(&e->kind, sizeof(e->kind), s0, s1);
    return h;
}

static int paint_cache_cmp_entry(const void* a, const void* b, void* udata) {
    (void)udata;
    const RdtPaintCacheEntry* ea = (const RdtPaintCacheEntry*)a;
    const RdtPaintCacheEntry* eb = (const RdtPaintCacheEntry*)b;
    if (ea->hash != eb->hash) return ea->hash < eb->hash ? -1 : 1;
    if (ea->kind != eb->kind) return ea->kind < eb->kind ? -1 : 1;
    if (!paint_cache_path_equals(ea->path, eb->path)) return 1;
    if (ea->kind == RDT_PAINT_CACHE_FILL_PATH) {
        if (ea->fill_rule != eb->fill_rule) return ea->fill_rule < eb->fill_rule ? -1 : 1;
        return paint_cache_color_equals(ea->color, eb->color) ? 0 : 1;
    }
    if (ea->kind == RDT_PAINT_CACHE_STROKE_PATH) {
        if (!paint_cache_color_equals(ea->color, eb->color)) return 1;
        if (ea->stroke_width != eb->stroke_width) return ea->stroke_width < eb->stroke_width ? -1 : 1;
        if (ea->stroke_cap != eb->stroke_cap) return ea->stroke_cap < eb->stroke_cap ? -1 : 1;
        if (ea->stroke_join != eb->stroke_join) return ea->stroke_join < eb->stroke_join ? -1 : 1;
        if (ea->dash_count != eb->dash_count) return ea->dash_count < eb->dash_count ? -1 : 1;
        if (ea->dash_phase != eb->dash_phase) return ea->dash_phase < eb->dash_phase ? -1 : 1;
        return paint_cache_float_array_equals(ea->dash_array, eb->dash_array, ea->dash_count) ? 0 : 1;
    }
    int value_count = ea->kind == RDT_PAINT_CACHE_LINEAR_GRADIENT ? 4 : 3;
    if (ea->fill_rule != eb->fill_rule) return ea->fill_rule < eb->fill_rule ? -1 : 1;
    if (ea->stop_count != eb->stop_count) return ea->stop_count < eb->stop_count ? -1 : 1;
    int value_cmp = memcmp(ea->gradient_values, eb->gradient_values,
                           (size_t)value_count * sizeof(float));
    if (value_cmp != 0) return value_cmp;
    return paint_cache_stops_equal(ea->stops, eb->stops, ea->stop_count) ? 0 : 1;
}

static bool paint_cache_ensure_locked() {
    if (g_paint_cache) return true;
    g_paint_cache = hashmap_new(sizeof(RdtPaintCacheEntry), RDT_PAINT_CACHE_MAX_ENTRIES, 0, 0,
        paint_cache_hash_entry, paint_cache_cmp_entry, NULL, NULL);
    return g_paint_cache != nullptr;
}

static void paint_cache_entry_free(RdtPaintCacheEntry* e);

static Tvg_Paint paint_cache_dup_fill_locked(uint64_t hash, RdtPaintCacheKind kind,
                                             const RdtPath* path, Color color,
                                             RdtFillRule rule) {
    if (!g_paint_cache) return nullptr;
    RdtPaintCacheEntry query = {};
    query.hash = hash;
    query.kind = kind;
    query.path = (RdtPath*)path;
    query.color = color;
    query.fill_rule = rule;
    const RdtPaintCacheEntry* e =
        (const RdtPaintCacheEntry*)hashmap_get(g_paint_cache, &query);
    if (e && paint_cache_entry_matches_fill((RdtPaintCacheEntry*)e, hash, kind, path, color, rule)) {
        return tvg_duplicate_paint_locked(e->paint);
    }
    return nullptr;
}

static Tvg_Paint paint_cache_dup_stroke_locked(uint64_t hash, const RdtPath* path,
                                               Color color, float width,
                                               RdtStrokeCap cap, RdtStrokeJoin join,
                                               const float* dash_array, int dash_count,
                                               float dash_phase) {
    if (!g_paint_cache) return nullptr;
    RdtPaintCacheEntry query = {};
    query.hash = hash;
    query.kind = RDT_PAINT_CACHE_STROKE_PATH;
    query.path = (RdtPath*)path;
    query.color = color;
    query.stroke_width = width;
    query.stroke_cap = cap;
    query.stroke_join = join;
    query.dash_array = (float*)dash_array;
    query.dash_count = dash_count;
    query.dash_phase = dash_phase;
    const RdtPaintCacheEntry* e =
        (const RdtPaintCacheEntry*)hashmap_get(g_paint_cache, &query);
    if (e && paint_cache_entry_matches_stroke((RdtPaintCacheEntry*)e, hash, path, color,
                                              width, cap, join, dash_array, dash_count, dash_phase)) {
        return tvg_duplicate_paint_locked(e->paint);
    }
    return nullptr;
}

static Tvg_Paint paint_cache_dup_gradient_locked(uint64_t hash, RdtPaintCacheKind kind,
                                                 const RdtPath* path, const float* values,
                                                 const RdtGradientStop* stops, int stop_count,
                                                 RdtFillRule rule) {
    if (!g_paint_cache) return nullptr;
    int value_count = kind == RDT_PAINT_CACHE_LINEAR_GRADIENT ? 4 : 3;
    RdtPaintCacheEntry query = {};
    query.hash = hash;
    query.kind = kind;
    query.path = (RdtPath*)path;
    query.fill_rule = rule;
    query.stop_count = stop_count;
    query.stops = (RdtGradientStop*)stops;
    memcpy(query.gradient_values, values, (size_t)value_count * sizeof(float));
    const RdtPaintCacheEntry* e =
        (const RdtPaintCacheEntry*)hashmap_get(g_paint_cache, &query);
    if (e && paint_cache_entry_matches_gradient((RdtPaintCacheEntry*)e, hash, kind, path,
                                                values, stops, stop_count, rule)) {
        return tvg_duplicate_paint_locked(e->paint);
    }
    return nullptr;
}

static void paint_cache_insert_entry_locked(RdtPaintCacheEntry* entry) {
    if (!entry || !entry->paint || g_paint_cache_count >= RDT_PAINT_CACHE_MAX_ENTRIES) return;
    if (!paint_cache_ensure_locked()) {
        paint_cache_entry_free(entry);
        return;
    }
    hashmap_set(g_paint_cache, entry);
    if (hashmap_oom(g_paint_cache)) {
        paint_cache_entry_free(entry);
    } else {
        mem_free(entry);
        g_paint_cache_count++;
    }
}

static Tvg_Paint paint_cache_prepare_store_locked(Tvg_Paint existing,
                                                  Tvg_Paint paint) {
    if (existing) {
        tvg_paint_unref(paint, true);
        pthread_mutex_unlock(&g_paint_cache_mutex);
        return existing;
    }
    if (g_paint_cache_count >= RDT_PAINT_CACHE_MAX_ENTRIES) {
        pthread_mutex_unlock(&g_paint_cache_mutex);
        return nullptr;
    }

    Tvg_Paint draw = tvg_duplicate_paint_locked(paint);
    if (!draw) pthread_mutex_unlock(&g_paint_cache_mutex);
    return draw;
}

static RdtPaintCacheEntry* paint_cache_new_entry(uint64_t hash,
                                                 RdtPaintCacheKind kind,
                                                 const RdtPath* path,
                                                 Tvg_Paint paint) {
    RdtPaintCacheEntry* entry = (RdtPaintCacheEntry*)mem_calloc(
        1, sizeof(RdtPaintCacheEntry), MEM_CAT_CACHE_IMAGE);
    entry->kind = kind;
    entry->hash = hash;
    entry->path = rdt_path_clone(path);
    entry->paint = paint;
    return entry;
}

static Tvg_Paint paint_cache_store_fill(uint64_t hash, RdtPaintCacheKind kind,
                                        const RdtPath* path, Color color,
                                        RdtFillRule rule, Tvg_Paint paint) {
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint existing = paint_cache_dup_fill_locked(hash, kind, path, color, rule);
    Tvg_Paint draw = paint_cache_prepare_store_locked(existing, paint);
    if (!draw || existing) return draw;

    RdtPaintCacheEntry* e = paint_cache_new_entry(hash, kind, path, paint);
    e->color = color;
    e->fill_rule = rule;
    paint_cache_insert_entry_locked(e);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    return draw;
}

static Tvg_Paint paint_cache_store_stroke(uint64_t hash, const RdtPath* path,
                                          Color color, float width,
                                          RdtStrokeCap cap, RdtStrokeJoin join,
                                          const float* dash_array, int dash_count,
                                          float dash_phase, Tvg_Paint paint) {
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint existing = paint_cache_dup_stroke_locked(hash, path, color, width, cap, join,
                                                       dash_array, dash_count, dash_phase);
    Tvg_Paint draw = paint_cache_prepare_store_locked(existing, paint);
    if (!draw || existing) return draw;

    RdtPaintCacheEntry* e = paint_cache_new_entry(
        hash, RDT_PAINT_CACHE_STROKE_PATH, path, paint);
    e->color = color;
    e->stroke_width = width;
    e->stroke_cap = cap;
    e->stroke_join = join;
    e->dash_phase = dash_phase;
    e->dash_count = dash_count;
    if (dash_array && dash_count > 0) {
        e->dash_array = (float*)mem_alloc((size_t)dash_count * sizeof(float), MEM_CAT_CACHE_IMAGE);
        memcpy(e->dash_array, dash_array, (size_t)dash_count * sizeof(float));
    }
    paint_cache_insert_entry_locked(e);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    return draw;
}

static Tvg_Paint paint_cache_store_gradient(uint64_t hash, RdtPaintCacheKind kind,
                                            const RdtPath* path, const float* values,
                                            const RdtGradientStop* stops, int stop_count,
                                            RdtFillRule rule, Tvg_Paint paint) {
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint existing = paint_cache_dup_gradient_locked(hash, kind, path, values, stops, stop_count, rule);
    Tvg_Paint draw = paint_cache_prepare_store_locked(existing, paint);
    if (!draw || existing) return draw;

    int value_count = kind == RDT_PAINT_CACHE_LINEAR_GRADIENT ? 4 : 3;
    RdtPaintCacheEntry* e = paint_cache_new_entry(hash, kind, path, paint);
    e->fill_rule = rule;
    memcpy(e->gradient_values, values, (size_t)value_count * sizeof(float));
    e->stop_count = stop_count;
    e->stops = (RdtGradientStop*)mem_alloc((size_t)stop_count * sizeof(RdtGradientStop), MEM_CAT_CACHE_IMAGE);
    memcpy(e->stops, stops, (size_t)stop_count * sizeof(RdtGradientStop));
    paint_cache_insert_entry_locked(e);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    return draw;
}

static void paint_cache_entry_release_fields(RdtPaintCacheEntry* e) {
    if (!e) return;
    if (e->path) rdt_path_free(e->path);
    if (e->dash_array) mem_free(e->dash_array);
    if (e->stops) mem_free(e->stops);
    if (e->paint) tvg_paint_unref(e->paint, true);
}

static void paint_cache_entry_free(RdtPaintCacheEntry* e) {
    if (!e) return;
    paint_cache_entry_release_fields(e);
    mem_free(e);
}

static bool paint_cache_free_scan(const void* item, void* udata) {
    (void)udata;
    RdtPaintCacheEntry copy = *(const RdtPaintCacheEntry*)item;
    paint_cache_entry_release_fields(&copy);
    return true;
}

static void paint_cache_clear_all() {
    pthread_mutex_lock(&g_paint_cache_mutex);
    HashMap* cache = g_paint_cache;
    g_paint_cache = nullptr;
    g_paint_cache_count = 0;
    pthread_mutex_unlock(&g_paint_cache_mutex);

    if (cache) {
        hashmap_scan(cache, paint_cache_free_scan, NULL);
        hashmap_free(cache);
    }
}

static Tvg_Paint image_paint_cache_dup_locked(const uint32_t* pixels, int src_w, int src_h,
                                              int src_stride, uint64_t generation) {
    if (!g_image_paint_cache) return nullptr;
    RdtImagePaintCacheEntry query = {};
    query.pixels = pixels;
    query.generation = generation;
    query.src_w = src_w;
    query.src_h = src_h;
    query.src_stride = src_stride;
    const RdtImagePaintCacheEntry* e =
        (const RdtImagePaintCacheEntry*)hashmap_get(g_image_paint_cache, &query);
    if (e) {
        return tvg_duplicate_paint_locked(e->paint);
    }
    return nullptr;
}

static uint64_t image_paint_cache_hash(const void* item, uint64_t s0, uint64_t s1) {
    const RdtImagePaintCacheEntry* e = (const RdtImagePaintCacheEntry*)item;
    uint64_t h = hashmap_sip(&e->pixels, sizeof(e->pixels), s0, s1);
    h ^= hashmap_sip(&e->generation, sizeof(e->generation), s0, s1);
    h ^= hashmap_sip(&e->src_w, sizeof(e->src_w), s0, s1);
    h ^= hashmap_sip(&e->src_h, sizeof(e->src_h), s0, s1);
    h ^= hashmap_sip(&e->src_stride, sizeof(e->src_stride), s0, s1);
    return h;
}

static int image_paint_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const RdtImagePaintCacheEntry* ea = (const RdtImagePaintCacheEntry*)a;
    const RdtImagePaintCacheEntry* eb = (const RdtImagePaintCacheEntry*)b;
    if (ea->pixels != eb->pixels) return ea->pixels < eb->pixels ? -1 : 1;
    if (ea->generation != eb->generation) return ea->generation < eb->generation ? -1 : 1;
    if (ea->src_w != eb->src_w) return ea->src_w < eb->src_w ? -1 : 1;
    if (ea->src_h != eb->src_h) return ea->src_h < eb->src_h ? -1 : 1;
    if (ea->src_stride != eb->src_stride) return ea->src_stride < eb->src_stride ? -1 : 1;
    return 0;
}

static bool image_paint_cache_ensure_locked() {
    if (g_image_paint_cache) return true;
    g_image_paint_cache = hashmap_new(sizeof(RdtImagePaintCacheEntry),
        RDT_IMAGE_PAINT_CACHE_MAX_ENTRIES, 0, 0,
        image_paint_cache_hash, image_paint_cache_cmp, NULL, NULL);
    return g_image_paint_cache != nullptr;
}

static Tvg_Paint image_paint_cache_store(const uint32_t* pixels, int src_w, int src_h,
                                         int src_stride, uint64_t generation, Tvg_Paint paint) {
    pthread_mutex_lock(&g_image_paint_cache_mutex);
    Tvg_Paint existing = image_paint_cache_dup_locked(pixels, src_w, src_h, src_stride, generation);
    if (existing) {
        tvg_paint_unref(paint, true);
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        return existing;
    }
    if (g_image_paint_cache_count >= RDT_IMAGE_PAINT_CACHE_MAX_ENTRIES) {
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        return nullptr;
    }

    Tvg_Paint draw = tvg_duplicate_paint_locked(paint);
    if (!draw) {
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        return nullptr;
    }

    if (!image_paint_cache_ensure_locked()) {
        tvg_paint_unref(draw, true);
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        return nullptr;
    }

    RdtImagePaintCacheEntry e = {};
    e.pixels = pixels;
    e.generation = generation;
    e.src_w = src_w;
    e.src_h = src_h;
    e.src_stride = src_stride;
    e.paint = paint;
    hashmap_set(g_image_paint_cache, &e);
    if (hashmap_oom(g_image_paint_cache)) {
        tvg_paint_unref(draw, true);
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        return nullptr;
    } else {
        g_image_paint_cache_count++;
    }
    pthread_mutex_unlock(&g_image_paint_cache_mutex);
    return draw;
}

static bool image_paint_cache_free_scan(const void* item, void* udata) {
    (void)udata;
    const RdtImagePaintCacheEntry* entry = (const RdtImagePaintCacheEntry*)item;
    if (entry->paint) tvg_paint_unref(entry->paint, true);
    return true;
}

static void image_paint_cache_clear_all() {
    pthread_mutex_lock(&g_image_paint_cache_mutex);
    HashMap* cache = g_image_paint_cache;
    g_image_paint_cache = nullptr;
    g_image_paint_cache_count = 0;
    pthread_mutex_unlock(&g_image_paint_cache_mutex);

    if (cache) {
        hashmap_scan(cache, image_paint_cache_free_scan, NULL);
        hashmap_free(cache);
    }
}

static bool matrix_is_projective(const RdtMatrix* transform) {
    if (!transform) return false;
    return fabsf(transform->e31) > 0.000001f ||
           fabsf(transform->e32) > 0.000001f ||
           fabsf(transform->e33 - 1.0f) > 0.000001f;
}

static void matrix_apply_point(const RdtMatrix* transform, float x, float y,
                               float* out_x, float* out_y) {
    if (!transform) {
        *out_x = x;
        *out_y = y;
        return;
    }
    float w = transform->e31 * x + transform->e32 * y + transform->e33;
    if (fabsf(w) < 0.000001f) w = 1.0f;
    *out_x = (transform->e11 * x + transform->e12 * y + transform->e13) / w;
    *out_y = (transform->e21 * x + transform->e22 * y + transform->e23) / w;
}

// Replay a path's commands onto a ThorVG shape
static void path_replay(RdtPath* p, Tvg_Paint shape) {
    for (int i = 0; i < p->count; i++) {
        RdtPath::Entry* e = &p->entries[i];
        switch (e->cmd) {
            case RdtPath::CMD_MOVE:
                tvg_shape_move_to(shape, e->args[0], e->args[1]);
                break;
            case RdtPath::CMD_LINE:
                tvg_shape_line_to(shape, e->args[0], e->args[1]);
                break;
            case RdtPath::CMD_CUBIC:
                tvg_shape_cubic_to(shape, e->args[0], e->args[1],
                                   e->args[2], e->args[3],
                                   e->args[4], e->args[5]);
                break;
            case RdtPath::CMD_CLOSE:
                tvg_shape_close(shape);
                break;
            case RdtPath::CMD_RECT:
                tvg_shape_append_rect(shape, e->args[0], e->args[1],
                                      e->args[2], e->args[3],
                                      e->args[4], e->args[5], true);
                break;
            case RdtPath::CMD_CIRCLE:
                tvg_shape_append_circle(shape, e->args[0], e->args[1],
                                        e->args[2], e->args[3], true);
                break;
        }
    }
}

static void path_replay_projective(RdtPath* p, Tvg_Paint shape, const RdtMatrix* transform) {
    for (int i = 0; i < p->count; i++) {
        RdtPath::Entry* e = &p->entries[i];
        float x, y;
        switch (e->cmd) {
            case RdtPath::CMD_MOVE:
                matrix_apply_point(transform, e->args[0], e->args[1], &x, &y);
                tvg_shape_move_to(shape, x, y);
                break;
            case RdtPath::CMD_LINE:
                matrix_apply_point(transform, e->args[0], e->args[1], &x, &y);
                tvg_shape_line_to(shape, x, y);
                break;
            case RdtPath::CMD_CUBIC: {
                float x1, y1, x2, y2, x3, y3;
                matrix_apply_point(transform, e->args[0], e->args[1], &x1, &y1);
                matrix_apply_point(transform, e->args[2], e->args[3], &x2, &y2);
                matrix_apply_point(transform, e->args[4], e->args[5], &x3, &y3);
                tvg_shape_cubic_to(shape, x1, y1, x2, y2, x3, y3);
                break;
            }
            case RdtPath::CMD_CLOSE:
                tvg_shape_close(shape);
                break;
            case RdtPath::CMD_RECT: {
                float x0 = e->args[0];
                float y0 = e->args[1];
                float x1 = x0 + e->args[2];
                float y1 = y0 + e->args[3];
                matrix_apply_point(transform, x0, y0, &x, &y);
                tvg_shape_move_to(shape, x, y);
                matrix_apply_point(transform, x1, y0, &x, &y);
                tvg_shape_line_to(shape, x, y);
                matrix_apply_point(transform, x1, y1, &x, &y);
                tvg_shape_line_to(shape, x, y);
                matrix_apply_point(transform, x0, y1, &x, &y);
                tvg_shape_line_to(shape, x, y);
                tvg_shape_close(shape);
                break;
            }
            case RdtPath::CMD_CIRCLE: {
                const int segment_count = 24;
                for (int j = 0; j < segment_count; j++) {
                    float angle = ((float)j / (float)segment_count) * 2.0f * (float)M_PI;
                    float px = e->args[0] + cosf(angle) * e->args[2];
                    float py = e->args[1] + sinf(angle) * e->args[3];
                    matrix_apply_point(transform, px, py, &x, &y);
                    if (j == 0) {
                        tvg_shape_move_to(shape, x, y);
                    } else {
                        tvg_shape_line_to(shape, x, y);
                    }
                }
                tvg_shape_close(shape);
                break;
            }
        }
    }
}

// Central draw-and-remove: resets target, pushes, draws, syncs, removes.
// This encapsulates the ThorVG immediate-mode workaround.
static void tvg_draw_paint_now(RdtVectorImpl* impl, Tvg_Paint shape) {
    // reset target to prevent ThorVG from clearing previously-drawn content
    tvg_swcanvas_set_target(impl->canvas, impl->pixels, impl->stride,
                            impl->width, impl->height, TVG_COLORSPACE_ABGR8888);
    // for tiled rendering: wrap the shape in a scene translated by (-tile_offset_x, -tile_offset_y)
    // so page-absolute coordinates map to tile-relative coordinates
    if (impl->tile_offset_x != 0.0f || impl->tile_offset_y != 0.0f) {
        Tvg_Paint scene = tvg_scene_new();
        tvg_scene_push(scene, shape);
        tvg_paint_translate(scene, -impl->tile_offset_x, -impl->tile_offset_y);
        shape = scene;
    }
    tvg_canvas_push(impl->canvas, shape);
    tvg_canvas_draw(impl->canvas, false);
    tvg_canvas_sync(impl->canvas);
    tvg_canvas_remove(impl->canvas, NULL);
}

static void tvg_flush_batch_scene(RdtVectorImpl* impl) {
    if (!impl || !impl->batch_scene) return;
    Tvg_Paint scene = impl->batch_scene;
    impl->batch_scene = nullptr;
    tvg_draw_paint_now(impl, scene);
}

static void tvg_push_draw_remove(RdtVectorImpl* impl, Tvg_Paint shape) {
    if (impl->batch_depth > 0) {
        if (!impl->batch_scene) {
            impl->batch_scene = tvg_scene_new();
        }
        if (impl->batch_scene) {
            tvg_scene_push(impl->batch_scene, shape);
            return;
        }
    }
    tvg_draw_paint_now(impl, shape);
}

// Apply optional transform to a shape
static void apply_transform(Tvg_Paint shape, const RdtMatrix* transform) {
    if (!transform) return;
    Tvg_Matrix m = { transform->e11, transform->e12, transform->e13,
                     transform->e21, transform->e22, transform->e23,
                     transform->e31, transform->e32, transform->e33 };
    tvg_paint_set_transform(shape, &m);
}

// Create a clip mask shape from a path (solid black fill for alpha masking)
static Tvg_Paint create_clip_mask(RdtPath* clip_path, const RdtMatrix* transform) {
    Tvg_Paint clip = tvg_shape_new();
    if (matrix_is_projective(transform)) {
        path_replay_projective(clip_path, clip, transform);
        transform = nullptr;
    } else {
        path_replay(clip_path, clip);
    }
    tvg_shape_set_fill_color(clip, 0, 0, 0, 255);
    apply_transform(clip, transform);
    return clip;
}

// ============================================================================
// Lifecycle
// ============================================================================

static const RdtVectorCaps g_tvg_caps = {
    RDT_VECTOR_BACKEND_THORVG,
    "ThorVG",
    true,   // vector_paths
    true,   // rounded_rects
    true,   // gradients
    true,   // nested_clips
    true,   // image_scaling
    true,   // picture_svg
    true,   // picture_duplication
    true,   // svg_dom_pictures
    false,  // opacity_group
    false,  // blend_modes
#ifdef __APPLE__
    true,   // gaussian_blur
#else
    false,  // gaussian_blur
#endif
    false,  // color_matrix_filters
    false,  // native_text_runs
    true,   // vector_batching
    false,  // premultiplied_surface
    true,   // tile_offsets
    true,   // clip_depth_save_restore
};

void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    RdtVectorImpl* impl = (RdtVectorImpl*)mem_calloc(1, sizeof(RdtVectorImpl), MEM_CAT_RENDER);
    impl->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    impl->pixels = pixels;
    impl->width = w;
    impl->height = h;
    impl->stride = stride;

    Tvg_Result result = tvg_swcanvas_set_target(impl->canvas, pixels, stride,
                                                 w, h, TVG_COLORSPACE_ABGR8888);
    if (result != TVG_RESULT_SUCCESS) {
        log_error("rdt_vector_init: tvg_swcanvas_set_target failed result=%d", result);
    }

    vec->impl = impl;
    log_debug("rdt_vector_init: ThorVG backend ready %dx%d stride=%d", w, h, stride);
}

void rdt_vector_destroy(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    tvg_flush_batch_scene(impl);
    if (impl->canvas) tvg_canvas_destroy(impl->canvas);
    mem_free(impl);
    vec->impl = nullptr;
}

void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    tvg_flush_batch_scene(impl);
    impl->pixels = pixels;
    impl->width = w;
    impl->height = h;
    impl->stride = stride;
    tvg_swcanvas_set_target(impl->canvas, pixels, stride, w, h, TVG_COLORSPACE_ABGR8888);
}

void rdt_vector_set_tile_offset_y(RdtVector* vec, float offset_y) {
    if (!vec || !vec->impl) return;
    vec->impl->tile_offset_y = offset_y;
}

void rdt_vector_set_tile_offset_x(RdtVector* vec, float offset_x) {
    if (!vec || !vec->impl) return;
    vec->impl->tile_offset_x = offset_x;
}

const RdtVectorCaps* rdt_vector_get_caps(const RdtVector* vec) {
    (void)vec;
    return &g_tvg_caps;
}

bool rdt_vector_get_target(const RdtVector* vec, RdtVectorTarget* out) {
    if (!vec || !vec->impl || !out) return false;
    out->pixels = vec->impl->pixels;
    out->width = vec->impl->width;
    out->height = vec->impl->height;
    out->stride = vec->impl->stride;
    out->tile_offset_x = vec->impl->tile_offset_x;
    out->tile_offset_y = vec->impl->tile_offset_y;
    return out->pixels && out->width > 0 && out->height > 0 && out->stride > 0;
}

void rdt_vector_begin_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    vec->impl->batch_depth++;
}

void rdt_vector_flush_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    tvg_flush_batch_scene(vec->impl);
}

void rdt_vector_end_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    if (impl->batch_depth <= 0) {
        log_error("rdt_vector_end_batch: unbalanced batch end");
        return;
    }
    impl->batch_depth--;
    if (impl->batch_depth == 0) {
        tvg_flush_batch_scene(impl);
    }
}

// ============================================================================
// Path construction
// ============================================================================

RdtPath* rdt_path_new(void) {
    RdtPath* p = (RdtPath*)mem_calloc(1, sizeof(RdtPath), MEM_CAT_RENDER);
    return p;
}

void rdt_path_move_to(RdtPath* p, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_MOVE);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_line_to(RdtPath* p, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_LINE);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_cubic_to(RdtPath* p, float cx1, float cy1,
                       float cx2, float cy2, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_CUBIC);
    if (!e) return;
    e->args[0] = cx1; e->args[1] = cy1;
    e->args[2] = cx2; e->args[3] = cy2;
    e->args[4] = x; e->args[5] = y;
}

void rdt_path_close(RdtPath* p) {
    path_append_entry(p, RdtPath::CMD_CLOSE);
}

void rdt_path_add_rect(RdtPath* p, float x, float y, float w, float h,
                       float rx, float ry) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_RECT);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
    e->args[2] = w; e->args[3] = h;
    e->args[4] = rx; e->args[5] = ry;
}

void rdt_path_add_circle(RdtPath* p, float cx, float cy, float rx, float ry) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_CIRCLE);
    if (!e) return;
    e->args[0] = cx; e->args[1] = cy;
    e->args[2] = rx; e->args[3] = ry;
}

void rdt_path_free(RdtPath* p) {
    if (!p) return;
    mem_free(p->entries);
    mem_free(p);
}

RdtPath* rdt_path_clone(const RdtPath* src) {
    if (!src) return nullptr;
    RdtPath* dst = (RdtPath*)mem_alloc(sizeof(RdtPath), MEM_CAT_RENDER);
    dst->count = src->count;
    dst->capacity = src->count;  // tight allocation
    if (src->count > 0) {
        size_t sz = src->count * sizeof(RdtPath::Entry);
        dst->entries = (RdtPath::Entry*)mem_alloc(sz, MEM_CAT_RENDER);
        memcpy(dst->entries, src->entries, sz);
    } else {
        dst->entries = nullptr;
    }
    return dst;
}

static void path_bounds_include_point(bool* has_point, float* left, float* top,
                                      float* right, float* bottom,
                                      float x, float y) {
    if (!*has_point) {
        *left = *right = x;
        *top = *bottom = y;
        *has_point = true;
        return;
    }
    if (x < *left) *left = x;
    if (x > *right) *right = x;
    if (y < *top) *top = y;
    if (y > *bottom) *bottom = y;
}

static void path_bounds_include_rect(bool* has_point, float* left, float* top,
                                     float* right, float* bottom,
                                     float x, float y, float w, float h) {
    float x0 = w < 0 ? x + w : x;
    float x1 = w < 0 ? x : x + w;
    float y0 = h < 0 ? y + h : y;
    float y1 = h < 0 ? y : y + h;
    path_bounds_include_point(has_point, left, top, right, bottom, x0, y0);
    path_bounds_include_point(has_point, left, top, right, bottom, x1, y1);
}

bool rdt_path_get_bounds(const RdtPath* p, float* left, float* top,
                         float* right, float* bottom) {
    if (!p || !left || !top || !right || !bottom) return false;

    bool has_point = false;
    float l = 0, t = 0, r = 0, b = 0;
    for (int i = 0; i < p->count; i++) {
        const RdtPath::Entry* e = &p->entries[i];
        switch (e->cmd) {
            case RdtPath::CMD_MOVE:
            case RdtPath::CMD_LINE:
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          e->args[0], e->args[1]);
                break;
            case RdtPath::CMD_CUBIC:
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          e->args[0], e->args[1]);
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          e->args[2], e->args[3]);
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          e->args[4], e->args[5]);
                break;
            case RdtPath::CMD_RECT:
                path_bounds_include_rect(&has_point, &l, &t, &r, &b,
                                         e->args[0], e->args[1],
                                         e->args[2], e->args[3]);
                break;
            case RdtPath::CMD_CIRCLE:
                path_bounds_include_rect(&has_point, &l, &t, &r, &b,
                                         e->args[0] - e->args[2],
                                         e->args[1] - e->args[3],
                                         e->args[2] * 2.0f,
                                         e->args[3] * 2.0f);
                break;
            case RdtPath::CMD_CLOSE:
                break;
        }
    }
    if (!has_point) return false;
    *left = l;
    *top = t;
    *right = r;
    *bottom = b;
    return true;
}

bool rdt_path_visit(const RdtPath* p, RdtPathVisitFn fn, void* context) {
    if (!p || !fn) return false;
    for (int i = 0; i < p->count; i++) {
        const RdtPath::Entry* e = &p->entries[i];
        RdtPathCommand command = RDT_PATH_MOVE;
        int arg_count = 0;
        switch (e->cmd) {
        case RdtPath::CMD_MOVE:
            command = RDT_PATH_MOVE;
            arg_count = 2;
            break;
        case RdtPath::CMD_LINE:
            command = RDT_PATH_LINE;
            arg_count = 2;
            break;
        case RdtPath::CMD_CUBIC:
            command = RDT_PATH_CUBIC;
            arg_count = 6;
            break;
        case RdtPath::CMD_CLOSE:
            command = RDT_PATH_CLOSE;
            arg_count = 0;
            break;
        case RdtPath::CMD_RECT:
            command = RDT_PATH_RECT;
            arg_count = 6;
            break;
        case RdtPath::CMD_CIRCLE:
            command = RDT_PATH_CIRCLE;
            arg_count = 4;
            break;
        }
        if (!fn(context, command, e->args, arg_count)) return false;
    }
    return true;
}

// ============================================================================
// Fill
// ============================================================================

// forward declare clip-aware draw helper (defined in Clipping section below)
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape);

static bool tvg_draw_cached_paint(RdtVectorImpl* impl, Tvg_Paint cached,
                                  const RdtMatrix* transform) {
    if (!cached) return false;
    apply_transform(cached, transform);
    tvg_push_draw_remove_clipped(impl, cached);
    return true;
}

static void tvg_draw_stored_or_original(RdtVectorImpl* impl, Tvg_Paint stored,
                                        Tvg_Paint original,
                                        const RdtMatrix* transform) {
    Tvg_Paint paint = stored ? stored : original;
    apply_transform(paint, transform);
    tvg_push_draw_remove_clipped(impl, paint);
}

static uint64_t tvg_paint_hash_color(RdtPaintCacheKind type, RdtPath* path,
                                     Color color) {
    uint64_t hash = rdt_paint_hash_common(type, path);
    rdt_hash_bytes(&hash, &color.r, sizeof(color.r));
    rdt_hash_bytes(&hash, &color.g, sizeof(color.g));
    rdt_hash_bytes(&hash, &color.b, sizeof(color.b));
    rdt_hash_bytes(&hash, &color.a, sizeof(color.a));
    return hash;
}

static void tvg_shape_apply_stroke_style(Tvg_Paint shape, Color color, float width,
                                         RdtStrokeCap cap, RdtStrokeJoin join,
                                         const float* dash_array, int dash_count,
                                         float dash_phase) {
    tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
    tvg_shape_set_stroke_width(shape, width);
    Tvg_Stroke_Cap tvg_cap = cap == RDT_CAP_ROUND ? TVG_STROKE_CAP_ROUND
        : cap == RDT_CAP_SQUARE ? TVG_STROKE_CAP_SQUARE : TVG_STROKE_CAP_BUTT;
    Tvg_Stroke_Join tvg_join = join == RDT_JOIN_ROUND ? TVG_STROKE_JOIN_ROUND
        : join == RDT_JOIN_BEVEL ? TVG_STROKE_JOIN_BEVEL : TVG_STROKE_JOIN_MITER;
    tvg_shape_set_stroke_cap(shape, tvg_cap);
    tvg_shape_set_stroke_join(shape, tvg_join);
    if (dash_array && dash_count > 0) {
        tvg_shape_set_stroke_dash(shape, dash_array, dash_count, dash_phase);
    }
}

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    if (matrix_is_projective(transform)) {
        Tvg_Paint shape = tvg_shape_new();
        path_replay_projective(p, shape, transform);
        tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
        if (rule == RDT_FILL_EVEN_ODD) {
            tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
        }
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }

    uint64_t hash = tvg_paint_hash_color(RDT_PAINT_CACHE_FILL_PATH, p, color);
    rdt_hash_bytes(&hash, &rule, sizeof(rule));
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint cached = paint_cache_dup_fill_locked(hash, RDT_PAINT_CACHE_FILL_PATH, p, color, rule);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    if (tvg_draw_cached_paint(impl, cached, transform)) return;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }
    Tvg_Paint draw = paint_cache_store_fill(hash, RDT_PAINT_CACHE_FILL_PATH, p, color, rule, shape);
    tvg_draw_stored_or_original(impl, draw, shape, transform);
}

void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, 0, 0, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, rx, ry, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    tvg_push_draw_remove_clipped(impl, shape);
}

// ============================================================================
// Stroke
// ============================================================================

void rdt_stroke_path(RdtVector* vec, RdtPath* p, Color color, float width,
                     RdtStrokeCap cap, RdtStrokeJoin join,
                     const float* dash_array, int dash_count, float dash_phase,
                     const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    if (matrix_is_projective(transform)) {
        Tvg_Paint shape = tvg_shape_new();
        path_replay_projective(p, shape, transform);
        tvg_shape_apply_stroke_style(shape, color, width, cap, join,
                                     dash_array, dash_count, dash_phase);
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }

    uint64_t hash = tvg_paint_hash_color(RDT_PAINT_CACHE_STROKE_PATH, p, color);
    rdt_hash_bytes(&hash, &width, sizeof(width));
    rdt_hash_bytes(&hash, &cap, sizeof(cap));
    rdt_hash_bytes(&hash, &join, sizeof(join));
    rdt_hash_bytes(&hash, &dash_count, sizeof(dash_count));
    rdt_hash_bytes(&hash, &dash_phase, sizeof(dash_phase));
    if (dash_array && dash_count > 0) {
        rdt_hash_bytes(&hash, dash_array, (size_t)dash_count * sizeof(float));
    }
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint cached = paint_cache_dup_stroke_locked(hash, p, color, width, cap, join,
                                                     dash_array, dash_count, dash_phase);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    if (tvg_draw_cached_paint(impl, cached, transform)) return;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    tvg_shape_apply_stroke_style(shape, color, width, cap, join,
                                 dash_array, dash_count, dash_phase);

    Tvg_Paint draw = paint_cache_store_stroke(hash, p, color, width, cap, join,
                                              dash_array, dash_count, dash_phase, shape);
    tvg_draw_stored_or_original(impl, draw, shape, transform);
}

// ============================================================================
// Gradient fill
// ============================================================================

static void tvg_gradient_set_stops(Tvg_Gradient gradient,
                                   const RdtGradientStop* stops,
                                   int stop_count) {
    Tvg_Color_Stop* tvg_stops = LAMBDA_ALLOCA(stop_count, Tvg_Color_Stop);
    for (int index = 0; index < stop_count; index++) {
        tvg_stops[index].offset = stops[index].offset;
        tvg_stops[index].r = stops[index].r;
        tvg_stops[index].g = stops[index].g;
        tvg_stops[index].b = stops[index].b;
        tvg_stops[index].a = stops[index].a;
    }
    tvg_gradient_set_color_stops(gradient, tvg_stops, stop_count);
}

static void tvg_gradient_apply_transform(Tvg_Gradient gradient,
                                         const RdtMatrix* transform) {
    if (!transform) return;
    Tvg_Matrix matrix = {transform->e11, transform->e12, transform->e13,
                         transform->e21, transform->e22, transform->e23,
                         transform->e31, transform->e32, transform->e33};
    tvg_gradient_set_transform(gradient, &matrix);
}

static uint64_t tvg_gradient_hash(RdtPaintCacheKind kind, RdtPath* path,
                                  const float* values, size_t values_size,
                                  const RdtGradientStop* stops, int stop_count,
                                  RdtFillRule rule,
                                  const RdtMatrix* gradient_transform) {
    uint64_t hash = rdt_paint_hash_common(kind, path);
    rdt_hash_bytes(&hash, values, values_size);
    rdt_hash_bytes(&hash, &stop_count, sizeof(stop_count));
    rdt_hash_bytes(&hash, stops, (size_t)stop_count * sizeof(RdtGradientStop));
    rdt_hash_bytes(&hash, &rule, sizeof(rule));
    if (gradient_transform) rdt_hash_bytes(&hash, gradient_transform, sizeof(RdtMatrix));
    return hash;
}

void rdt_fill_linear_gradient(RdtVector* vec, RdtPath* p,
                              float x1, float y1, float x2, float y2,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* impl = vec->impl;
    if (matrix_is_projective(transform)) {
        float tx1, ty1, tx2, ty2;
        RdtMatrix combined = gradient_transform
            ? rdt_matrix_multiply(transform, gradient_transform)
            : *transform;
        matrix_apply_point(&combined, x1, y1, &tx1, &ty1);
        matrix_apply_point(&combined, x2, y2, &tx2, &ty2);
        Tvg_Paint shape = tvg_shape_new();
        path_replay_projective(p, shape, transform);
        if (rule == RDT_FILL_EVEN_ODD) {
            tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
        }
        Tvg_Gradient grad = tvg_linear_gradient_new();
        tvg_linear_gradient_set(grad, tx1, ty1, tx2, ty2);
        tvg_gradient_set_stops(grad, stops, stop_count);
        tvg_shape_set_gradient(shape, grad);
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }
    float values[4] = {x1, y1, x2, y2};

    uint64_t hash = tvg_gradient_hash(RDT_PAINT_CACHE_LINEAR_GRADIENT, p,
        values, sizeof(values), stops, stop_count, rule, gradient_transform);
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint cached = paint_cache_dup_gradient_locked(hash, RDT_PAINT_CACHE_LINEAR_GRADIENT,
                                                       p, values, stops, stop_count, rule);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    if (tvg_draw_cached_paint(impl, cached, transform)) return;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }

    Tvg_Gradient grad = tvg_linear_gradient_new();
    tvg_linear_gradient_set(grad, x1, y1, x2, y2);

    tvg_gradient_set_stops(grad, stops, stop_count);
    tvg_gradient_apply_transform(grad, gradient_transform);
    tvg_shape_set_gradient(shape, grad);

    Tvg_Paint draw = paint_cache_store_gradient(hash, RDT_PAINT_CACHE_LINEAR_GRADIENT,
                                                p, values, stops, stop_count, rule, shape);
    tvg_draw_stored_or_original(impl, draw, shape, transform);
}

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* p,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* impl = vec->impl;
    if (matrix_is_projective(transform)) {
        float tcx, tcy, trx, try_;
        RdtMatrix combined = gradient_transform
            ? rdt_matrix_multiply(transform, gradient_transform)
            : *transform;
        matrix_apply_point(&combined, cx, cy, &tcx, &tcy);
        matrix_apply_point(&combined, cx + r, cy, &trx, &try_);
        float tr = sqrtf((trx - tcx) * (trx - tcx) + (try_ - tcy) * (try_ - tcy));
        Tvg_Paint shape = tvg_shape_new();
        path_replay_projective(p, shape, transform);
        if (rule == RDT_FILL_EVEN_ODD) {
            tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
        }
        Tvg_Gradient grad = tvg_radial_gradient_new();
        tvg_radial_gradient_set(grad, tcx, tcy, tr, tcx, tcy, 0);
        tvg_gradient_set_stops(grad, stops, stop_count);
        tvg_shape_set_gradient(shape, grad);
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }
    float values[3] = {cx, cy, r};

    uint64_t hash = tvg_gradient_hash(RDT_PAINT_CACHE_RADIAL_GRADIENT, p,
        values, sizeof(values), stops, stop_count, rule, gradient_transform);
    pthread_mutex_lock(&g_paint_cache_mutex);
    Tvg_Paint cached = paint_cache_dup_gradient_locked(hash, RDT_PAINT_CACHE_RADIAL_GRADIENT,
                                                       p, values, stops, stop_count, rule);
    pthread_mutex_unlock(&g_paint_cache_mutex);
    if (tvg_draw_cached_paint(impl, cached, transform)) return;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }

    Tvg_Gradient grad = tvg_radial_gradient_new();
    tvg_radial_gradient_set(grad, cx, cy, r, cx, cy, 0);

    tvg_gradient_set_stops(grad, stops, stop_count);
    tvg_gradient_apply_transform(grad, gradient_transform);
    tvg_shape_set_gradient(shape, grad);

    Tvg_Paint draw = paint_cache_store_gradient(hash, RDT_PAINT_CACHE_RADIAL_GRADIENT,
                                                p, values, stops, stop_count, rule, shape);
    tvg_draw_stored_or_original(impl, draw, shape, transform);
}

// ============================================================================
// Clipping
// ============================================================================

// ThorVG doesn't have a clip stack. We implement clipping by applying alpha masks
// to each shape at draw time. For rdt_push_clip / rdt_pop_clip, we store the
// clip path and apply it when shapes are drawn.
//
// For this ThorVG backend, push_clip/pop_clip use a simple approach:
// save the clip path, and rdt_fill_*/rdt_stroke_* check for active clips.
// However, since the current usage pattern is: push_clip → draw → pop_clip
// (always bracketed tightly), and ThorVG applies masks per-shape,
// we implement this by storing the clip state and applying it in the
// push_draw_remove helper.

// For simplicity and correctness, we use the same approach as the existing
// render code: create a mask shape for each drawn shape and call
// tvg_paint_set_mask_method.

// But this means we need to thread the clip through all draw calls.
// A cleaner approach for this backend: since clips are always bracketed,
// store the active clip path(s) in the impl and apply in tvg_push_draw_remove.

// Active clip state
#define RDT_INITIAL_CLIP_DEPTH 8

struct ClipEntry {
    RdtPath* path;
    RdtMatrix transform;
    bool has_transform;
};

// We extend RdtVectorImpl with clip state via thread-local storage to avoid
// modifying the struct definition visible to other code.
static thread_local ClipEntry s_clip_inline_stack[RDT_INITIAL_CLIP_DEPTH];
static thread_local ClipEntry* s_clip_stack = s_clip_inline_stack;
static thread_local int s_clip_depth = 0;
static thread_local int s_clip_capacity = RDT_INITIAL_CLIP_DEPTH;

static bool ensure_clip_capacity(int needed_depth) {
    if (needed_depth <= s_clip_capacity) return true;
    int new_capacity = s_clip_capacity * 2;
    while (new_capacity < needed_depth) new_capacity *= 2;
    ClipEntry* new_stack = (ClipEntry*)mem_calloc((size_t)new_capacity, sizeof(ClipEntry), MEM_CAT_RENDER);
    if (!new_stack) {
        log_warn("[RAD_CAP_TVG_CLIP] failed to grow clip stack to depth %d", needed_depth);
        return false;
    }
    if (s_clip_depth > 0) {
        memcpy(new_stack, s_clip_stack, (size_t)s_clip_depth * sizeof(ClipEntry));
    }
    if (s_clip_stack != s_clip_inline_stack) {
        mem_free(s_clip_stack);
    }
    s_clip_stack = new_stack;
    s_clip_capacity = new_capacity;
    log_warn("[RAD_CAP_TVG_CLIP] grew clip stack to depth %d", s_clip_capacity);
    return true;
}

static void release_heap_clip_stack_if_empty() {
    if (s_clip_depth != 0 || s_clip_stack == s_clip_inline_stack) return;
    mem_free(s_clip_stack);
    s_clip_stack = s_clip_inline_stack;
    s_clip_capacity = RDT_INITIAL_CLIP_DEPTH;
}

void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !clip_path) return;
    if (!ensure_clip_capacity(s_clip_depth + 1)) {
        return;
    }

    // copy the path for the duration of the clip
    RdtPath* copy = (RdtPath*)mem_calloc(1, sizeof(RdtPath), MEM_CAT_RENDER);
    if (clip_path->count > 0) {
        copy->entries = (RdtPath::Entry*)mem_alloc(clip_path->count * sizeof(RdtPath::Entry), MEM_CAT_RENDER);
        memcpy(copy->entries, clip_path->entries, clip_path->count * sizeof(RdtPath::Entry));
        copy->count = clip_path->count;
        copy->capacity = clip_path->count;
    }

    ClipEntry* entry = &s_clip_stack[s_clip_depth++];
    entry->path = copy;
    entry->has_transform = (transform != nullptr);
    if (transform) entry->transform = *transform;
}

void rdt_pop_clip(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    if (s_clip_depth <= 0) {
        log_error("rdt_pop_clip: clip stack underflow");
        return;
    }
    s_clip_depth--;
    ClipEntry* entry = &s_clip_stack[s_clip_depth];
    rdt_path_free(entry->path);
    entry->path = nullptr;
    release_heap_clip_stack_if_empty();
}

int rdt_clip_save_depth() {
    int saved = s_clip_depth;
    s_clip_depth = 0;
    return saved;
}

void rdt_clip_restore_depth(int saved_depth) {
    if (!ensure_clip_capacity(saved_depth)) {
        saved_depth = s_clip_capacity;
    }
    s_clip_depth = saved_depth;
}

// Apply active clip masks to a shape (called before tvg_push_draw_remove)
// Multiple clips are composed as intersection by nesting masks: the innermost
// mask is masked by the next outer one, and finally applied to the shape.
static void apply_clip_masks(Tvg_Paint shape) {
    if (s_clip_depth <= 0) return;

    // Build a single composed mask from all clip entries.
    // Start from the outermost clip (index 0) and nest inward.
    Tvg_Paint composed = create_clip_mask(s_clip_stack[0].path,
        s_clip_stack[0].has_transform ? &s_clip_stack[0].transform : nullptr);

    for (int i = 1; i < s_clip_depth; i++) {
        ClipEntry* entry = &s_clip_stack[i];
        if (!entry->path) continue;
        Tvg_Paint inner = create_clip_mask(entry->path,
            entry->has_transform ? &entry->transform : nullptr);
        // mask the inner clip by the composed (outer) clip → intersection
        tvg_paint_set_mask_method(inner, composed, TVG_MASK_METHOD_ALPHA);
        composed = inner;
    }

    tvg_paint_set_mask_method(shape, composed, TVG_MASK_METHOD_ALPHA);
}

// Clip-aware version of tvg_push_draw_remove
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape) {
    if (s_clip_depth > 0) {
        apply_clip_masks(shape);
    }
    tvg_push_draw_remove(impl, shape);
}

// ============================================================================
// Image drawing
// ============================================================================

void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform, uint64_t resource_generation) {
    if (!vec || !vec->impl || !pixels) return;
    RdtVectorImpl* impl = vec->impl;
    int tight_stride = src_w * 4;
    // rdt_draw_image receives uint32_t row stride; ThorVG raw upload needs byte rows.
    int src_stride_bytes = src_stride * 4;
    if (src_w <= 0 || src_h <= 0 || src_stride < src_w) return;

    if (resource_generation != 0) {
        pthread_mutex_lock(&g_image_paint_cache_mutex);
        Tvg_Paint cached = image_paint_cache_dup_locked(pixels, src_w, src_h, src_stride,
                                                        resource_generation);
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        if (cached) {
            tvg_picture_set_size(cached, dst_w, dst_h);
            if (opacity < 255) {
                tvg_paint_set_opacity(cached, opacity);
            }
            if (transform) {
                RdtMatrix translate = rdt_matrix_translate(dst_x, dst_y);
                RdtMatrix composed = rdt_matrix_multiply(transform, &translate);
                apply_transform(cached, &composed);
            } else {
                tvg_paint_translate(cached, dst_x, dst_y);
            }
            tvg_push_draw_remove_clipped(impl, cached);
            return;
        }
    }

    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return;

    const uint32_t* raw_pixels = pixels;
    uint32_t* tight_pixels = nullptr;
    if (src_stride_bytes != tight_stride) {
        // ThorVG raw images have no stride parameter; copy strided rows tightly
        // so clipped/offset image draws cannot make ThorVG read past each row.
        tight_pixels = (uint32_t*)mem_alloc((size_t)src_w * src_h * 4, MEM_CAT_IMAGE);
        if (!tight_pixels) {
            tvg_paint_unref(pic, true);
            return;
        }
        const unsigned char* src = (const unsigned char*)pixels;
        unsigned char* dst = (unsigned char*)tight_pixels;
        for (int y = 0; y < src_h; y++) {
            memcpy(dst + (size_t)y * tight_stride, src + (size_t)y * src_stride_bytes, (size_t)tight_stride);
        }
        raw_pixels = tight_pixels;
    }

    bool copy_pixels = (resource_generation != 0 || tight_pixels != nullptr);
    if (tvg_picture_load_raw(pic, (uint32_t*)raw_pixels, src_w, src_h,
        TVG_COLORSPACE_ABGR8888, copy_pixels) != TVG_RESULT_SUCCESS) {
        log_debug("rdt_draw_image: tvg_picture_load_raw failed");
        if (tight_pixels) mem_free(tight_pixels);
        tvg_paint_unref(pic, true);
        return;
    }
    if (tight_pixels) mem_free(tight_pixels);

    if (resource_generation != 0) {
        Tvg_Paint draw = image_paint_cache_store(pixels, src_w, src_h, src_stride,
                                                 resource_generation, pic);
        if (draw) {
            pic = draw;
        }
    }

    tvg_picture_set_size(pic, dst_w, dst_h);

    if (opacity < 255) {
        tvg_paint_set_opacity(pic, opacity);
    }

    if (transform) {
        RdtMatrix translate = rdt_matrix_translate(dst_x, dst_y);
        RdtMatrix composed = rdt_matrix_multiply(transform, &translate);
        apply_transform(pic, &composed);
    } else {
        tvg_paint_translate(pic, dst_x, dst_y);
    }
    tvg_push_draw_remove_clipped(impl, pic);
}

// ============================================================================
// Picture (SVG / vector image files)
// ============================================================================

// Helper: parse SVG content into an Element tree and locate the root <svg>.
// On success, allocates a private Pool and Input, returns a fully-populated
// RdtPicture* in SVG_DOM mode.  Returns nullptr on failure.
static RdtPicture* svg_picture_create(const char* data, int size, const char* source_path) {
    if (!data || size <= 0) return nullptr;

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_MEDIA, "rdt.vector.tvg");
    if (!pool) {
        log_error("svg_picture_create: pool_create failed");
        return nullptr;
    }
    Input* input = Input::create(pool, nullptr);
    if (!input) {
        mem_pool_destroy(pool);
        return nullptr;
    }
    input->ui_mode = false;

    // html5_parse_svg_document expects a null-terminated string and applies the
    // same SVG tag/attribute correction path used by inline HTML SVG.
    char* buf = (char*)mem_alloc(size + 1, MEM_CAT_RENDER);
    if (!buf) { mem_pool_destroy(pool); return nullptr; }
    memcpy(buf, data, size);
    buf[size] = '\0';
    Element* svg_root = html5_parse_svg_document(input, buf, nullptr);
    mem_free(buf);

    if (!input->root.item || input->root.item == ITEM_ERROR) {
        log_error("svg_picture_create: html5_parse_svg_document failed");
        mem_pool_destroy(pool);
        return nullptr;
    }
    if (!svg_root) {
        log_error("svg_picture_create: no <svg> root element found");
        mem_pool_destroy(pool);
        return nullptr;
    }

    // intrinsic size
    SvgIntrinsicSize isz = calculate_svg_intrinsic_size(svg_root);
    float w = isz.width  > 0 ? isz.width  : 300.0f;
    float h = isz.height > 0 ? isz.height : 150.0f;

    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    p->kind = RdtPicture::KIND_SVG_DOM;
    p->input = input;
    p->pool = pool;
    p->owns_pool = true;
    atomic_store32(&p->ref_count, 1);
    p->owner = nullptr;
    p->svg_root = svg_root;
    p->source_path = source_path ? mem_strdup(source_path, MEM_CAT_RENDER) : nullptr;  // RETAINED_FIELD_OK: TVG picture-local field, mem_strdup-owned, not a retained DOM field
    p->width = w;
    p->height = h;
    p->has_transform = false;
    return p;
}

RdtPicture* rdt_picture_load(const char* path) {
    if (!path) return nullptr;

    pthread_mutex_lock(&g_picture_cache_mutex);
    RdtPicture* cached = picture_cache_dup_path_locked(path);
    pthread_mutex_unlock(&g_picture_cache_mutex);
    if (cached) {
        log_debug("rdt_picture_load: cache hit for %s", path);
        return cached;
    }

    // Read file content; the SVG path is parsed by Radiant (not ThorVG).
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        log_error("rdt_picture_load: failed to open %s", path);
        return nullptr;
    }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz <= 0) { fclose(fp); return nullptr; }
    char* buf = (char*)mem_alloc((size_t)fsz, MEM_CAT_RENDER);
    if (!buf) { fclose(fp); return nullptr; }
    size_t rd = fread(buf, 1, (size_t)fsz, fp);
    fclose(fp);

    RdtPicture* p = svg_picture_create(buf, (int)rd, path);
    mem_free(buf);
    if (!p) {
        log_error("rdt_picture_load: failed to parse %s", path);
        return nullptr;
    }

    pthread_mutex_lock(&g_picture_cache_mutex);
    cached = picture_cache_dup_path_locked(path);
    if (cached) {
        pthread_mutex_unlock(&g_picture_cache_mutex);
        rdt_picture_free(p);
        return cached;
    }
    bool inserted = picture_cache_insert_path_locked(path, p);
    RdtPicture* result = inserted ? rdt_picture_dup(p) : p;
    pthread_mutex_unlock(&g_picture_cache_mutex);
    return result;
}

RdtPicture* rdt_picture_load_data(const char* data, int size, const char* mime_type) {
    if (!data || size <= 0) return nullptr;
    // Currently the only vector format Radiant pictures handle is SVG.
    // mime_type is accepted for API compatibility; non-svg types fall through
    // to the SVG parser which will fail gracefully.
    uint64_t hash = rdt_picture_data_hash(data, size, mime_type);
    pthread_mutex_lock(&g_picture_cache_mutex);
    RdtPicture* cached = picture_cache_dup_data_locked(data, size, mime_type, hash);
    pthread_mutex_unlock(&g_picture_cache_mutex);
    if (cached) {
        log_debug("rdt_picture_load_data: cache hit (%d bytes, mime=%s)",
                  size, mime_type ? mime_type : "");
        return cached;
    }

    RdtPicture* p = svg_picture_create(data, size, nullptr);
    if (!p) return nullptr;

    pthread_mutex_lock(&g_picture_cache_mutex);
    cached = picture_cache_dup_data_locked(data, size, mime_type, hash);
    if (cached) {
        pthread_mutex_unlock(&g_picture_cache_mutex);
        rdt_picture_free(p);
        return cached;
    }
    bool inserted = picture_cache_insert_data_locked(data, size, mime_type, hash, p);
    RdtPicture* result = inserted ? rdt_picture_dup(p) : p;
    pthread_mutex_unlock(&g_picture_cache_mutex);
    return result;
}

RdtPicture* RdtPicture::dup() {
    if (kind == RdtPicture::KIND_TVG_PAINT) {
        if (!paint) return nullptr;
        Tvg_Paint dup = tvg_paint_duplicate(paint);
        if (!dup) return nullptr;
        RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
        p->kind = RdtPicture::KIND_TVG_PAINT;
        atomic_store32(&p->ref_count, 1);
        p->paint = dup;
        p->width = width;
        p->height = height;
        return p;
    }
    // SVG_DOM duplicates have independent draw state but retain the parsed
    // owner so cache eviction cannot free the shared Element tree underneath.
    if (!rdt_picture_retain_svg_owner(this)) return nullptr;
    RdtPicture* owner = rdt_picture_svg_owner(this);
    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    if (!p) {
        rdt_picture_free(owner);
        return nullptr;
    }
    p->kind = RdtPicture::KIND_SVG_DOM;
    p->input = owner->input;
    p->pool = owner->pool;
    p->owns_pool = false;
    p->owner = owner;
    p->svg_root = owner->svg_root;
    p->source_path = owner->source_path;  // RETAINED_FIELD_OK: TVG picture-local field, not a retained DOM field
    p->width = width;
    p->height = height;
    p->transform = transform;
    p->has_transform = has_transform;
    return p;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    return pic ? pic->dup() : nullptr;
}

static const char* rdt_picture_elem_attr(Element* element, const char* attr_name) {
    if (!element || !element->data || !attr_name) return nullptr;
    TypeElmt* elem_type = (TypeElmt*)element->type;
    if (!elem_type) return nullptr;
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return nullptr;

    size_t attr_len = strlen(attr_name);
    // INT_CAST_OK: attribute names in parsed SVG markup fit the shape-key API.
    ShapeEntry* field = typemap_hash_lookup(map_type, attr_name, (int)attr_len);
    if (!field || !field->type || field->type->type_id != LMD_TYPE_STRING) return nullptr;

    Item value = map_shape_field_to_item(element->data, field);
    String* str_val = value.get_safe_string();
    return str_val ? str_val->chars : nullptr;
}

static Element* rdt_picture_find_id_recursive(Element* elem, const char* id) {
    if (!elem || !id) return nullptr;
    const char* elem_id = rdt_picture_elem_attr(elem, "id");
    if (elem_id && strcmp(elem_id, id) == 0) return elem;
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        if (get_type_id(child) != LMD_TYPE_ELEMENT) continue;
        Element* found = rdt_picture_find_id_recursive(child.element, id);
        if (found) return found;
    }
    return nullptr;
}

Element* rdt_picture_get_svg_root(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM) return nullptr;
    return pic->svg_root;
}

Element* rdt_picture_find_svg_element_by_id(RdtPicture* pic, const char* id) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM || !id || !*id) return nullptr;
    return rdt_picture_find_id_recursive(pic->svg_root, id);
}

Pool* rdt_picture_get_pool(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM) return nullptr;
    return pic->pool;
}

const char* rdt_picture_get_source_path(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM) return nullptr;
    return pic->source_path;
}

void rdt_picture_get_size(RdtPicture* pic, float* w, float* h) {
    if (!pic) { if (w) *w = 0; if (h) *h = 0; return; }
    if (w) *w = pic->width;
    if (h) *h = pic->height;
}

void rdt_picture_set_size(RdtPicture* pic, float w, float h) {
    if (!pic) return;
    pic->width = w;
    pic->height = h;
}

// Internal helper: render a SVG_DOM picture into vec at the picture's stored
// w/h, applying the optional transform.  No tvg_* calls.
static void svg_dom_picture_draw(RdtVector* vec, RdtPicture* pic,
                                 uint8_t opacity, const RdtMatrix* transform) {
    if (!pic->svg_root) return;
    // Compose the explicit per-picture transform (set via set_transform) and
    // the caller-provided transform.  Caller transform applies after.
    RdtMatrix base = rdt_matrix_identity();
    if (pic->has_transform) base = pic->transform;
    if (transform) base = rdt_matrix_multiply(transform, &base);
    render_svg_to_vec_via_display_list(vec, pic->svg_root, pic->width, pic->height,
                      pic->pool, 1.0f, g_picture_font_ctx, &base,
                      nullptr, nullptr, pic->source_path,
                      (float)opacity / 255.0f);
}

static bool draw_svg_dom_picture_if_needed(RdtVector* vec, RdtPicture* pic,
                                           uint8_t opacity,
                                           const RdtMatrix* transform) {
    if (pic->kind != RdtPicture::KIND_SVG_DOM) return false;
    svg_dom_picture_draw(vec, pic, opacity, transform);
    return true;
}

static void prepare_tvg_picture_paint(RdtPicture* pic, Tvg_Paint paint,
                                      uint8_t opacity,
                                      const RdtMatrix* transform) {
    if (pic->width > 0 && pic->height > 0) {
        tvg_picture_set_size(paint, pic->width, pic->height);
    }
    if (opacity < 255) tvg_paint_set_opacity(paint, opacity);
    apply_transform(paint, transform);
}

void rdt_picture_draw(RdtVector* vec, RdtPicture* pic,
                      uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pic) return;

    if (draw_svg_dom_picture_if_needed(vec, pic, opacity, transform)) return;

    // KIND_TVG_PAINT
    if (!pic->paint) return;
    RdtVectorImpl* impl = vec->impl;

    prepare_tvg_picture_paint(pic, pic->paint, opacity, transform);

    // For pictures, we need to push fresh since tvg_push_draw_remove
    // will remove and destroy the paint. Clone or re-load would be needed
    // for repeated draws. For now, one-shot semantics.
    tvg_push_draw_remove_clipped(impl, pic->paint);
    pic->paint = nullptr; // consumed by canvas remove
}

void rdt_picture_draw_dup(RdtVector* vec, RdtPicture* pic,
                          uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pic) return;

    if (draw_svg_dom_picture_if_needed(vec, pic, opacity, transform)) return;

    // KIND_TVG_PAINT: thread-safe duplicate so the original stays intact.
    if (!pic->paint) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint dup = tvg_duplicate_paint_locked(pic->paint);
    if (!dup) return;

    prepare_tvg_picture_paint(pic, dup, opacity, transform);
    tvg_push_draw_remove_clipped(impl, dup);
    // original pic->paint is NOT consumed
}

void RdtPicture::release() {
    RdtPicture* pic = this;
    if (pic->kind == RdtPicture::KIND_SVG_DOM) {
        RdtPicture* owner = pic->owner;
        if (owner) {
            mem_free(pic);
            pic = owner;
        }
        int32_t refs = atomic_dec32(&pic->ref_count);
        if (refs > 0) return;
        if (refs < 0) {
            log_error("rdt_picture_free: SVG picture ref_count underflow");
            return;
        }
        if (pic->pool) mem_pool_destroy(pic->pool);
        if (pic->source_path) mem_free(pic->source_path);
        // input is allocated from the pool; destroyed implicitly above
    } else {
        if (pic->paint) tvg_paint_unref(pic->paint, true);
    }
    mem_free(pic);
}

void rdt_picture_free(RdtPicture* pic) {
    if (pic) pic->release();
}

// ============================================================================
// Bridge: wrap ThorVG paint as RdtPicture (used by render_svg_inline.cpp)
// ============================================================================

RdtPicture* rdt_picture_take_tvg_paint(Tvg_Paint paint, float w, float h) {
    if (!paint) return nullptr;
    RdtPicture* pic = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    pic->kind = RdtPicture::KIND_TVG_PAINT;
    atomic_store32(&pic->ref_count, 1);
    pic->paint = paint;
    pic->width = w;
    pic->height = h;
    return pic;
}

void rdt_picture_set_transform(RdtPicture* pic, const RdtMatrix* m) {
    if (!pic || !m) return;
    if (pic->kind == RdtPicture::KIND_SVG_DOM) {
        pic->transform = *m;
        pic->has_transform = true;
        return;
    }
    if (!pic->paint) return;
    Tvg_Matrix tm;
    tm.e11 = m->e11; tm.e12 = m->e12; tm.e13 = m->e13;
    tm.e21 = m->e21; tm.e22 = m->e22; tm.e23 = m->e23;
    tm.e31 = m->e31; tm.e32 = m->e32; tm.e33 = m->e33;
    tvg_paint_set_transform(pic->paint, &tm);
}

// ============================================================================
// Engine lifecycle
// ============================================================================

void rdt_engine_init(int threads) {
    tvg_engine_init(threads);
    if (!g_vector_cache_owner.context) {
        g_vector_cache_owner.context = mem_context_create(
            mem_context_root(), MEM_ROLE_RENDER, "rdt.vector.engine");
        if (g_vector_cache_owner.context) {
            g_vector_cache_owner.registry_node = mem_register(
                g_vector_cache_owner.context, MEM_KIND_CACHE, MEM_ROLE_RENDER,
                "rdt.vector.caches", &g_vector_cache_owner, NULL,
                vector_cache_stat, vector_cache_destroy);
        }
    }
}

void rdt_engine_term(void) {
    if (g_vector_cache_owner.context && g_vector_cache_owner.registry_node) {
        // The cache registry is the single vector-engine owner; cascading its
        // context releases every cache before ThorVG itself is terminated.
        mem_context_destroy(g_vector_cache_owner.context);
    } else {
        vector_cache_destroy(&g_vector_cache_owner);
        if (g_vector_cache_owner.context) {
            mem_context_destroy(g_vector_cache_owner.context);
        }
    }
    g_vector_cache_owner.context = NULL;
    g_vector_cache_owner.registry_node = NULL;
    tvg_engine_term();
}

void rdt_set_font_context(struct FontContext* ctx) {
    g_picture_font_ctx = ctx;
}
