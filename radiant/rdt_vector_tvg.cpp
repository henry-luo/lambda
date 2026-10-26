#include "rdt_path_internal.hpp"
#include "render_metrics.hpp"
// rdt_vector_tvg.cpp — ThorVG backend for RdtVector
// All ThorVG C API calls are isolated in this file.
// Radiant rendering code calls only rdt_* functions.

#include "render.hpp"
#include "render_row_jobs.hpp"
#include "../lib/atomic.h"
#include "../lib/ref_count.h"
#include "../lib/log.h"
#include "../lib/lambda_alloca.h"
#include "../lib/mem_grow.hpp"
#include "../lib/mem_factory.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/hash.h"
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
    Tvg_Paint batch_root;
    bool batch_root_group;
    uint32_t* pixels;
    int width;
    int height;
    int stride;
    int batch_depth;
    bool opaque_rect_raster_enabled;
    uint64_t clip_mask_count;
    float tile_offset_x;  // physical-pixel X start of current tile (0 = full page)
    float tile_offset_y;  // physical-pixel Y start of current tile (0 = full page)
    lam::Own<Arena> projection_arena;
    ScratchArena projection_scratch;
};

static bool vector_raster_fill_rect(RdtVectorImpl* impl, float x, float y, float w, float h, Color color);


struct RdtPicture {
    // Two kinds of pictures:
    //  - SVG_DOM: Radiant-parsed SVG DOM (Element*). Used by rdt_picture_load*
    //    for file/data SVG.  Goes through the same code path as inline <svg>
    //    in HTML, so font weight/style/family resolution is uniform with HTML
    //    body text: drawn into a page it is painted into the recording
    //    (render_svg_record_picture); drawn off-screen it records and replays
    //    a private DisplayList.  Never stored in a DisplayList for replay.
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
    RefCount ref_count;     // SVG_DOM owner references, including cache and duplicate handles
    RdtPicture* owner;     // duplicate handles retain the owning SVG_DOM picture
    Element* svg_root;     // root <svg> element
    char* source_path;     // original file path for resolving nested SVG refs

    // Common
    float width;           // intrinsic width (from viewBox or width attr) or override via set_size
    float height;
    RdtMatrix transform;   // optional explicit transform (set via rdt_picture_set_transform)
    bool has_transform;
    double animation_time;

    RdtPicture* dup();
    void release();
};

// Process-wide font context for SVG-DOM pictures (set by ui_context).
// Inline <svg> uses RasterRenderContext->ui_context->font_ctx; standalone pictures
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

typedef struct RdtImagePaintCacheEntry {
    const uint32_t* pixels;
    uint64_t generation, identity;
    int src_w;
    int src_h;
    int src_stride;
    bool straight_alpha;
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
static void image_paint_cache_clear_all();

static bool vector_cache_stat(void* allocator, MemStatSample* sample) {
    (void)allocator;
    if (!sample) return false;
    pthread_mutex_lock(&g_picture_cache_mutex);
    size_t picture_count = (g_picture_path_cache ? hashmap_count(g_picture_path_cache) : 0) +
                           (g_picture_data_cache ? hashmap_count(g_picture_data_cache) : 0);
    pthread_mutex_unlock(&g_picture_cache_mutex);
    pthread_mutex_lock(&g_image_paint_cache_mutex);
    size_t image_count = g_image_paint_cache ? hashmap_count(g_image_paint_cache) : 0;
    pthread_mutex_unlock(&g_image_paint_cache_mutex);
    sample->alloc_count = picture_count + image_count;
    sample->bytes_in_use = picture_count * sizeof(RdtPictureCacheEntry) +
                           image_count * sizeof(RdtImagePaintCacheEntry);
    sample->bytes_reserved = sample->bytes_in_use;
    return true;
}

static void vector_cache_destroy(void* allocator) {
    (void)allocator;
    image_paint_cache_clear_all();
    picture_cache_clear_all();
}

// Mutex to serialize tvg_paint_duplicate calls (ThorVG Picture::duplicate is not thread-safe:
// it increments a shared non-atomic counter and may mutate the source loader's state).
static pthread_mutex_t g_tvg_dup_mutex = PTHREAD_MUTEX_INITIALIZER;

static Tvg_Paint rdt_tvg_shape_new() {
    RenderMetricTimer timer(&RenderPipelineMetrics::native_shape_ns);
    RENDER_METRIC_ADD(native_shapes, 1);
    return tvg_shape_new();
}

static void tvg_lock(pthread_mutex_t* mutex) {
    RenderMetricTimer timer(&RenderPipelineMetrics::native_lock_wait_ns);
    pthread_mutex_lock(mutex);
}

// ============================================================================
// Helpers
// ============================================================================

static uint64_t rdt_picture_data_hash(const char* data, int size, const char* mime_type) {
    uint64_t h = HASH_FNV1A_64_OFFSET_BASIS;
    if (size > 0) h = hash_fnv1a_64_extend(h, data, (size_t)size);
    if (mime_type) h = hash_fnv1a_64_extend_cstr(h, mime_type);
    return h;
}

static Tvg_Paint tvg_duplicate_paint_locked(Tvg_Paint paint) {
    if (!paint) return nullptr;
    RENDER_METRIC_ADD(native_duplicates, 1);
    tvg_lock(&g_tvg_dup_mutex);
    RenderMetricTimer timer(&RenderPipelineMetrics::native_duplicate_ns);
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
    return ref_count_retain(&owner->ref_count);
}

static void picture_cache_entry_release(const RdtPictureCacheEntry* entry) {
    if (!entry) return;
    // releasing an entry hands its key and data copies over
    lam::Temp<char> path_key(entry->path_key);
    lam::Temp<char> mime_key(entry->mime_key);
    lam::Temp<char> data_copy(entry->data_copy);
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
    return hashmap_hash_cstr(key, s0, s1);
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
    uint64_t h = hashmap_hash_bytes(&e->data_hash, sizeof(e->data_hash), s0, s1);
    h ^= hashmap_hash_bytes(&e->data_size, sizeof(e->data_size), s0, s1);
    const char* mime = e->mime_key ? e->mime_key : "";
    h ^= hashmap_hash_cstr(mime, s0, s1);
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
    lam::Temp<char> path_key(mem_strdup(path, MEM_CAT_CACHE_IMAGE));
    if (!path_key) return false;
    RdtPictureCacheEntry e = {};
    e.path_key = path_key.get();
    e.picture = picture;
    e.last_used = picture_cache_next_stamp_locked();
    const RdtPictureCacheEntry* replaced =
        (const RdtPictureCacheEntry*)hashmap_set(g_picture_path_cache, &e);
    if (hashmap_oom(g_picture_path_cache)) return false;
    path_key.release();  // the cache entry owns the key now
    if (replaced) picture_cache_entry_release(replaced);
    picture_cache_evict_to_capacity_locked(g_picture_path_cache);
    return true;
}

static bool picture_cache_insert_data_locked(const char* data, int size,
                                             const char* mime_type, uint64_t hash,
                                             RdtPicture* picture) {
    if (!data || size <= 0 || !picture) return false;
    if (!picture_cache_ensure_data_locked()) return false;
    lam::Temp<char> data_copy = lam::temp_array<char>((size_t)size, MEM_CAT_CACHE_IMAGE);
    if (!data_copy) return false;
    memcpy(data_copy.get(), data, (size_t)size);
    lam::Temp<char> mime_key(mem_strdup(mime_type ? mime_type : "", MEM_CAT_CACHE_IMAGE));
    if (!mime_key) return false;
    RdtPictureCacheEntry e = {};
    e.data_copy = data_copy.get();
    e.data_size = size;
    e.data_hash = hash;
    e.mime_key = mime_key.get();
    e.picture = picture;
    e.last_used = picture_cache_next_stamp_locked();
    const RdtPictureCacheEntry* replaced =
        (const RdtPictureCacheEntry*)hashmap_set(g_picture_data_cache, &e);
    if (hashmap_oom(g_picture_data_cache)) return false;
    // the cache entry owns the copies now
    data_copy.release();
    mime_key.release();
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
    tvg_lock(&g_picture_cache_mutex);
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

static Tvg_Paint image_paint_cache_dup_locked(const uint32_t* pixels, int src_w, int src_h,
                                              int src_stride, uint64_t generation, bool straight_alpha, uint64_t identity) {
    if (!g_image_paint_cache) return nullptr;
    RdtImagePaintCacheEntry query = {};
    query.pixels = pixels;
    query.generation = generation;query.identity=identity;
    query.src_w = src_w;
    query.src_h = src_h;
    query.src_stride = src_stride;
    query.straight_alpha = straight_alpha;
    const RdtImagePaintCacheEntry* e =
        (const RdtImagePaintCacheEntry*)hashmap_get(g_image_paint_cache, &query);
    if (e) {
        return tvg_duplicate_paint_locked(e->paint);
    }
    return nullptr;
}

static uint64_t image_paint_cache_hash(const void* item, uint64_t s0, uint64_t s1) {
    const RdtImagePaintCacheEntry* e = (const RdtImagePaintCacheEntry*)item;
    uint64_t h = hashmap_hash_bytes(&e->pixels, sizeof(e->pixels), s0, s1);
    h ^= hashmap_hash_bytes(&e->generation, sizeof(e->generation), s0, s1);
    h ^= hashmap_hash_bytes(&e->identity,sizeof(e->identity),s0,s1);
    h ^= hashmap_hash_bytes(&e->src_w, sizeof(e->src_w), s0, s1);
    h ^= hashmap_hash_bytes(&e->src_h, sizeof(e->src_h), s0, s1);
    h ^= hashmap_hash_bytes(&e->src_stride, sizeof(e->src_stride), s0, s1);
    h ^= hashmap_hash_bytes(&e->straight_alpha, sizeof(e->straight_alpha), s0, s1);
    return h;
}

static int image_paint_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const RdtImagePaintCacheEntry* ea = (const RdtImagePaintCacheEntry*)a;
    const RdtImagePaintCacheEntry* eb = (const RdtImagePaintCacheEntry*)b;
    if (ea->pixels != eb->pixels) return ea->pixels < eb->pixels ? -1 : 1;
    if(ea->identity!=eb->identity) return ea->identity<eb->identity?-1:1;
    if (ea->generation != eb->generation) return ea->generation < eb->generation ? -1 : 1;
    if (ea->src_w != eb->src_w) return ea->src_w < eb->src_w ? -1 : 1;
    if (ea->src_h != eb->src_h) return ea->src_h < eb->src_h ? -1 : 1;
    if (ea->src_stride != eb->src_stride) return ea->src_stride < eb->src_stride ? -1 : 1;
    if (ea->straight_alpha != eb->straight_alpha) return ea->straight_alpha ? 1 : -1;
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
                                         int src_stride, uint64_t generation, bool straight_alpha, uint64_t identity, Tvg_Paint paint) {
    tvg_lock(&g_image_paint_cache_mutex);
    Tvg_Paint existing = image_paint_cache_dup_locked(pixels, src_w, src_h, src_stride, generation, straight_alpha, identity);
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
    e.generation = generation;e.identity=identity;
    e.src_w = src_w;
    e.src_h = src_h;
    e.src_stride = src_stride;
    e.straight_alpha = straight_alpha;
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
    tvg_lock(&g_image_paint_cache_mutex);
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
    rdt_matrix_project_point(transform, x, y, out_x, out_y, 0.000001f);
}

static bool path_replay_bulk(const RdtPath* path, Tvg_Paint shape) {
    size_t point_count = 0;
    for (int i = 0; i < path->count; i++) {
        switch (path->entries[i].cmd) {
            case RdtPath::CMD_MOVE: case RdtPath::CMD_LINE: point_count++; break;
            case RdtPath::CMD_CUBIC: point_count += 3; break;
            case RdtPath::CMD_CLOSE: break;
            default: return false; // compact primitives preserve ThorVG's established contour expansion.
        }
    }
    if (!path->count || point_count > UINT32_MAX) return false;
    lam::Temp<Tvg_Path_Command> commands = lam::temp_array<Tvg_Path_Command>((size_t)path->count, MEM_CAT_RENDER);
    lam::Temp<Tvg_Point> points = lam::temp_array<Tvg_Point>(point_count, MEM_CAT_RENDER);
    if (!commands || !points) return false;
    RENDER_METRIC_ADD(native_path_temp_bytes, (size_t)path->count * sizeof(Tvg_Path_Command) + point_count * sizeof(Tvg_Point));
    size_t next = 0;
    for (int i = 0; i < path->count; i++) {
        const RdtPath::Entry& entry = path->entries[i];
        size_t count = 0;
        switch (entry.cmd) {
            case RdtPath::CMD_MOVE: commands[i] = TVG_PATH_COMMAND_MOVE_TO; count = 1; break;
            case RdtPath::CMD_LINE: commands[i] = TVG_PATH_COMMAND_LINE_TO; count = 1; break;
            case RdtPath::CMD_CUBIC: commands[i] = TVG_PATH_COMMAND_CUBIC_TO; count = 3; break;
            default: commands[i] = TVG_PATH_COMMAND_CLOSE; break;
        }
        for (size_t point = 0; point < count; point++) points[next++] = {entry.args[point * 2], entry.args[point * 2 + 1]};
    }
    return tvg_shape_append_path(shape, commands.get(), (uint32_t)path->count,
        points.get(), (uint32_t)point_count) == TVG_RESULT_SUCCESS;
}

// Replay a path's commands onto a ThorVG shape
static void path_replay(RdtPath* p, Tvg_Paint shape) {
    RenderMetricTimer timer(&RenderPipelineMetrics::native_path_ns);
    if (path_replay_bulk(p, shape)) return;
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

static void path_replay_projective(RdtVectorImpl* impl, RdtPath* p, Tvg_Paint shape,
    const RdtMatrix* transform, bool fill = true) {
    float left, top, right, bottom;
    if (rdt_path_get_bounds(p, &left, &top, &right, &bottom)) {
        float min_w = transform->e33 + fminf(transform->e31 * left, transform->e31 * right) +
            fminf(transform->e32 * top, transform->e32 * bottom);
        if (min_w <= 0.00001f) {
            // Divide only after clipping a viewer-plane crossing. Projecting
            // the original corners produces a mirrored, self-crossing contour.
            if (!impl->projection_arena) {
                impl->projection_arena = lam::own(mem_arena_create(mem_context_process(MEM_ROLE_RENDER),
                    MEM_ROLE_RENDER, "vector.projection"));
                if (!impl->projection_arena) return;
                scratch_init(&impl->projection_scratch, impl->projection_arena);
            }
            ScratchScope scratch(&impl->projection_scratch);
            RdtPath* projected = rdt_path_new();
            Rect viewport = {impl->tile_offset_x, impl->tile_offset_y,
                (float)impl->width, (float)impl->height};
            if (projected && render_path_project_to_viewport(projected, p, transform, viewport, fill, &scratch))
                path_replay(projected, shape);
            rdt_path_free(projected);
            return;
        }
    }
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
                    float angle = ((float)j / (float)segment_count) * math_tau_f();
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
    {
        RenderMetricTimer timer(&RenderPipelineMetrics::native_submit_ns);
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
    }
    { RenderMetricTimer timer(&RenderPipelineMetrics::native_draw_ns); tvg_canvas_draw(impl->canvas, false); }
    { RenderMetricTimer timer(&RenderPipelineMetrics::native_sync_ns); tvg_canvas_sync(impl->canvas); }
    { RenderMetricTimer timer(&RenderPipelineMetrics::native_remove_ns); tvg_canvas_remove(impl->canvas, NULL); }
}

static void apply_clip_masks(RdtVectorImpl* impl, Tvg_Paint shape);

static void tvg_finish_clip_segment(RdtVectorImpl* impl) {
    if (!impl || !impl->batch_scene) return;
    Tvg_Paint scene = impl->batch_scene;
    impl->batch_scene = nullptr;
    // seal the current clip into its group before changing the execution stack.
    apply_clip_masks(impl, scene);
    if (!impl->batch_root) impl->batch_root = scene;
    else {
        if (!impl->batch_root_group) {
            Tvg_Paint group = tvg_scene_new();
            if (!group) {
                tvg_draw_paint_now(impl, impl->batch_root);
                impl->batch_root = scene;
                return;
            }
            tvg_scene_push(group, impl->batch_root);
            impl->batch_root = group; impl->batch_root_group = true;
        }
        tvg_scene_push(impl->batch_root, scene);
    }
    RENDER_METRIC_ADD(clip_segments, 1);
}

static void tvg_flush_batch_scene(RdtVectorImpl* impl,
    uint64_t RenderPipelineMetrics::*reason = &RenderPipelineMetrics::flush_boundary) {
    tvg_finish_clip_segment(impl);
    if (!impl || !impl->batch_root) return;
    RENDER_METRIC_ADD(batch_flushes, 1);
    if (render_pipeline_metrics) __atomic_fetch_add(&(render_pipeline_metrics->*reason), (uint64_t)1, __ATOMIC_RELAXED);
    Tvg_Paint scene = impl->batch_root;
    impl->batch_root = nullptr;
    impl->batch_root_group = false;
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

// create a filled shape for geometric clipping.
static Tvg_Paint create_clip_mask(RdtVectorImpl* impl, RdtPath* clip_path,
    const RdtMatrix* transform, RdtFillRule rule) {
    Tvg_Paint clip = rdt_tvg_shape_new();
    if (matrix_is_projective(transform)) {
        path_replay_projective(impl, clip_path, clip, transform);
        transform = nullptr;
    } else {
        path_replay(clip_path, clip);
    }
    tvg_shape_set_fill_color(clip, 0, 0, 0, 255);
    if (rule == RDT_FILL_EVEN_ODD) tvg_shape_set_fill_rule(clip, TVG_FILL_RULE_EVEN_ODD);
    apply_transform(clip, transform);
    return clip;
}

// ============================================================================
// Lifecycle
// ============================================================================

static const RdtVectorCaps g_tvg_caps = {
    RDT_VECTOR_BACKEND_THORVG,
    lam::up("ThorVG"),
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
    RenderMetricTimer timer(&RenderPipelineMetrics::native_canvas_ns);
    RENDER_METRIC_ADD(native_canvases, 1);
    RdtVectorImpl* impl = (RdtVectorImpl*)mem_calloc(1, sizeof(RdtVectorImpl), MEM_CAT_RENDER); // OBJ_HEAP_OK: owned by RdtVector::impl; rdt_vector_destroy releases it
    impl->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    impl->pixels = pixels;
    impl->width = w;
    impl->height = h;
    impl->stride = stride;
    const char* opaque_rect_raster = getenv("RADIANT_TVG_RECT_RASTER");
    impl->opaque_rect_raster_enabled = !opaque_rect_raster || strcmp(opaque_rect_raster, "off") != 0;

    Tvg_Result result = tvg_swcanvas_set_target(impl->canvas, pixels, stride,
                                                 w, h, TVG_COLORSPACE_ABGR8888);
    if (result != TVG_RESULT_SUCCESS) {
        log_error("rdt_vector_init: tvg_swcanvas_set_target failed result=%d", result);
    }

    vec->impl = lam::own(impl);
    log_debug("rdt_vector_init: ThorVG backend ready %dx%d stride=%d", w, h, stride);
}

void rdt_vector_destroy(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_destroy);
    if (impl->canvas) tvg_canvas_destroy(impl->canvas);
    if (impl->projection_arena) {
        scratch_release(&impl->projection_scratch);
        mem_arena_destroy(impl->projection_arena);
    }
    lam::free_owned(vec->impl);
}

void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_target);
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
    out->pixels = lam::up(vec->impl->pixels);
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
        tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_end);
    }
}

uint64_t rdt_vector_clip_mask_count(const RdtVector* vec) {
    return vec && vec->impl ? vec->impl->clip_mask_count : 0;
}

// ============================================================================
// Fill
// ============================================================================

// forward declare clip-aware draw helper (defined in Clipping section below)
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape);

static void tvg_shape_apply_stroke_style(Tvg_Paint shape, Color color, float width,
                                         RdtStrokeCap cap, RdtStrokeJoin join,
                                         const float* dash_array, int dash_count,
                                         float dash_phase, float miter_limit) {
    tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
    tvg_shape_set_stroke_width(shape, width);
    Tvg_Stroke_Cap tvg_cap = cap == RDT_CAP_ROUND ? TVG_STROKE_CAP_ROUND
        : cap == RDT_CAP_SQUARE ? TVG_STROKE_CAP_SQUARE : TVG_STROKE_CAP_BUTT;
    Tvg_Stroke_Join tvg_join = join == RDT_JOIN_ROUND ? TVG_STROKE_JOIN_ROUND
        : join == RDT_JOIN_BEVEL ? TVG_STROKE_JOIN_BEVEL : TVG_STROKE_JOIN_MITER;
    tvg_shape_set_stroke_cap(shape, tvg_cap);
    tvg_shape_set_stroke_join(shape, tvg_join);
    tvg_shape_set_stroke_miterlimit(shape, miter_limit);
    if (dash_array && dash_count > 0) {
        tvg_shape_set_stroke_dash(shape, dash_array, dash_count, dash_phase);
    }
}

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    if (matrix_is_projective(transform)) {
        Tvg_Paint shape = rdt_tvg_shape_new();
        path_replay_projective(impl, p, shape, transform);
        tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
        if (rule == RDT_FILL_EVEN_ODD) {
            tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
        }
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }

    Tvg_Paint shape = rdt_tvg_shape_new();
    path_replay(p, shape);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }
    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    if (vector_raster_fill_rect(impl, x, y, w, h, color)) return;

    Tvg_Paint shape = rdt_tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, 0, 0, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = rdt_tvg_shape_new();
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
                     const RdtMatrix* transform, float miter_limit) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    if (matrix_is_projective(transform)) {
        Tvg_Paint shape = rdt_tvg_shape_new();
        path_replay_projective(impl, p, shape, transform, false);
        tvg_shape_apply_stroke_style(shape, color, width, cap, join,
                                     dash_array, dash_count, dash_phase, miter_limit);
        tvg_push_draw_remove_clipped(impl, shape);
        return;
    }

    Tvg_Paint shape = rdt_tvg_shape_new();
    path_replay(p, shape);

    tvg_shape_apply_stroke_style(shape, color, width, cap, join,
                                 dash_array, dash_count, dash_phase, miter_limit);

    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

// ============================================================================
// Gradient fill
// ============================================================================

static void tvg_gradient_set_stops(Tvg_Gradient gradient,
                                   const RdtGradientStop* stops,
                                   int stop_count) {
    size_t stops_size = (size_t)stop_count * sizeof(Tvg_Color_Stop);
    // CSS gradients may contain more stops than the bounded scratch stack can
    // hold; keep the fast stack path for normal gradients and spill large,
    // valid stop arrays to render-owned heap storage instead of aborting.
    bool heap_stops = stops_size > LAMBDA_ALLOCA_MAX_BYTES;
    lam::Temp<Tvg_Color_Stop> heap_buffer = heap_stops
        ? lam::temp_array<Tvg_Color_Stop>((size_t)stop_count, MEM_CAT_RENDER)
        : lam::Temp<Tvg_Color_Stop>();
    Tvg_Color_Stop* tvg_stops = heap_stops ? heap_buffer.get() : LAMBDA_ALLOCA(stop_count, Tvg_Color_Stop);
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

static void tvg_draw_gradient(RdtVector* vec, RdtPath* path, bool radial,
                               const float* values, const RdtGradientStop* stops,
                               int stop_count, RdtFillRule rule,
                               const RdtMatrix* transform, const RdtMatrix* gradient_transform,
                               const RdtGradientOptions* requested) {
    if (!vec || !vec->impl || !path || !stops || stop_count < 2) return;
    RdtGradientOptions options = requested ? *requested : RdtGradientOptions{};
    if (!rdt_gradient_options_valid(&options)) return;
    RdtVectorImpl* impl = vec->impl;
    bool projective = matrix_is_projective(transform);
    Tvg_Paint shape = rdt_tvg_shape_new();
    if (projective) path_replay_projective(impl, path, shape, transform);
    else path_replay(path, shape);
    if (rule == RDT_FILL_EVEN_ODD) tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    Tvg_Gradient gradient = radial ? tvg_radial_gradient_new() : tvg_linear_gradient_new();
    float points[4] = {values[0], values[1], radial ? 0.0f : values[2], radial ? 0.0f : values[3]};
    float radius = radial ? values[2] : 0.0f;
    float fx = options.has_focal ? options.fx : values[0];
    float fy = options.has_focal ? options.fy : values[1];
    float fr = options.has_focal ? options.fr : 0.0f;
    if (projective) {
        RdtMatrix combined = gradient_transform ? rdt_matrix_multiply(transform, gradient_transform) : *transform;
        matrix_apply_point(&combined, points[0], points[1], &points[0], &points[1]);
        if (radial) {
            float rx, ry, focal_radius_x, focal_radius_y;
            matrix_apply_point(&combined, values[0] + radius, values[1], &rx, &ry);
            matrix_apply_point(&combined, fx + fr, fy, &focal_radius_x, &focal_radius_y);
            matrix_apply_point(&combined, fx, fy, &fx, &fy);
            radius = hypotf(rx - points[0], ry - points[1]);
            fr = hypotf(focal_radius_x - fx, focal_radius_y - fy);
        } else matrix_apply_point(&combined, values[2], values[3], &points[2], &points[3]);
    }
    if (radial) tvg_radial_gradient_set(gradient, points[0], points[1], radius, fx, fy, fr);
    else tvg_linear_gradient_set(gradient, points[0], points[1], points[2], points[3]);
    tvg_gradient_set_stops(gradient, stops, stop_count);
    Tvg_Stroke_Fill spread = options.spread == RDT_GRADIENT_REFLECT ? TVG_STROKE_FILL_REFLECT
        : options.spread == RDT_GRADIENT_REPEAT ? TVG_STROKE_FILL_REPEAT : TVG_STROKE_FILL_PAD;
    tvg_gradient_set_spread(gradient, spread);
    if (!projective) tvg_gradient_apply_transform(gradient, gradient_transform);
    if (options.stroke_width > 0.0f) {
        Color white = {}; white.r = white.g = white.b = white.a = 255;
        tvg_shape_apply_stroke_style(shape, white, options.stroke_width,
            options.cap, options.join, options.dash_array, options.dash_count, options.dash_phase,
            options.miter_limit >= 1.0f ? options.miter_limit : 4.0f);
        tvg_shape_set_stroke_gradient(shape, gradient);
    } else tvg_shape_set_gradient(shape, gradient);
    if (!projective) apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

static bool rdt_fill_linear_gradient_analytic(RdtVector* vec, RdtPath* path,
        float x1, float y1, float x2, float y2, const RdtGradientStop* stops, int count,
        RdtFillRule rule, const RdtMatrix* transform, const RdtMatrix* gradient_transform,
        const RdtGradientOptions* requested) {
    if (!vec || !vec->impl || !path || !stops || count < 2) return true;
    RdtGradientOptions options = requested ? *requested : RdtGradientOptions{};
    if (!rdt_gradient_options_valid(&options)) return true;
    if (options.stroke_width > 0.0f) return false;
    double vx = (double)x2 - x1, vy = (double)y2 - y1;
    double extent = vx * vx + vy * vy;
    if (!(extent > 0.0) || !isfinite(extent)) return false;
    RdtMatrix frame = transform ? *transform : rdt_matrix_identity();
    if (gradient_transform) frame = rdt_matrix_multiply(&frame, gradient_transform);
    // projective gradients retain the backend path until inverse-perspective sampling is shared.
    // The backend's own tolerance decides: a composed 3D transform that cancels to a
    // translation (a back face under a turned leaf) leaves ~1e-18 of perspective residue,
    // which must not move it onto a rasteriser with a different stripe phase.
    if (matrix_is_projective(&frame)) return false;
    frame.e31 = frame.e32 = 0.0f;
    frame.e33 = 1.0f;
    RdtMatrix inverse;
    if (!rdt_matrix_invert_affine(&frame, &inverse)) return false;
    RdtVectorImpl* impl = vec->impl;
    Bound bounds;
    bool bounded = rdt_path_get_bounds(path, &bounds.left, &bounds.top, &bounds.right, &bounds.bottom);
    if (bounded && transform) bounded = rdt_matrix_project_rect_bounds(transform,
        bounds.left, bounds.top, bounds.right, bounds.bottom,
        &bounds.left, &bounds.top, &bounds.right, &bounds.bottom);
    // keep the caller's pixel lattice, including fractional capture origins.
    float left = 0.0f, top = 0.0f, right = (float)impl->width, bottom = (float)impl->height;
    if (bounded && isfinite(bounds.left) && isfinite(bounds.top) &&
            isfinite(bounds.right) && isfinite(bounds.bottom)) {
        left = fmaxf(left, floorf(bounds.left - impl->tile_offset_x));
        top = fmaxf(top, floorf(bounds.top - impl->tile_offset_y));
        right = fminf(right, ceilf(bounds.right - impl->tile_offset_x));
        bottom = fminf(bottom, ceilf(bounds.bottom - impl->tile_offset_y));
    }
    if (right <= left || bottom <= top) return true;
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top);
    size_t first_column = (size_t)left, first_row = (size_t)top;
    size_t count_pixels, bytes;
    if (!math_checked_mul(width, height, &count_pixels) ||
        !math_checked_mul(count_pixels, sizeof(uint32_t), &bytes) ||
        !render_memory_allow_allocation(nullptr, bytes)) return false;
    lam::Temp<uint32_t> coverage = lam::temp_array<uint32_t>(count_pixels, MEM_CAT_RENDER);
    if (!coverage) return false;
    memset(coverage.get(), 0, bytes);
    rdt_vector_flush_batch(vec);
    RdtVector mask = {};
    rdt_vector_init(&mask, coverage.get(), (int)width, (int)height, (int)width); // INT_CAST_OK: bounded raster pixel counts fit the caller's integer target
    if (!mask.impl) return false;
    float origin_x = impl->tile_offset_x + left, origin_y = impl->tile_offset_y + top;
    rdt_vector_set_tile_offset_x(&mask, origin_x);
    rdt_vector_set_tile_offset_y(&mask, origin_y);
    // vector paths and the active clip stack still determine antialiased coverage.
    rdt_fill_path(&mask, path, Color{0xffffffffu}, rule, transform);
    rdt_vector_flush_batch(&mask); rdt_vector_destroy(&mask);
    auto sample_color = [&](size_t column, size_t row) {
        float gx, gy;
        rdt_matrix_transform_point(&inverse, (float)column + 0.5f + origin_x,
            (float)row + 0.5f + origin_y, &gx, &gy);
        float position = (float)(((gx - (double)x1) * vx + (gy - (double)y1) * vy) / extent);
        if (options.spread == RDT_GRADIENT_REPEAT) position = math_wrap_positive_f(position, 1.0f);
        else if (options.spread == RDT_GRADIENT_REFLECT) {
            position = math_wrap_positive_f(position, 2.0f);
            if (position > 1.0f) position = 2.0f - position;
        }
        return render_sample_gradient_stops(stops, count, position, 1.0f);
    };
    lam::Temp<uint32_t> row_colors;
    // axis-aligned gradients reuse colors across scanlines; curved coverage remains per pixel.
    if (height > 1 && ((vy == 0.0 && inverse.e12 == 0.0f) ||
            (vx == 0.0 && inverse.e22 == 0.0f)) &&
            render_memory_allow_allocation(nullptr, width * sizeof(uint32_t))) {
        row_colors = lam::temp_array<uint32_t>(width, MEM_CAT_RENDER);
        if (row_colors) for (size_t column = 0; column < width; column++)
            row_colors[column] = sample_color(column, 0).c;
    }
    auto sample_rows = [&](int begin, int end) {
        for (size_t row = (size_t)begin; row < (size_t)end; row++) for (size_t column = 0; column < width; column++) {
            uint8_t alpha = (uint8_t)(coverage[row * width + column] >> 24);
            if (!alpha) continue;
            Color color = row_colors ? Color{row_colors[column]} : sample_color(column, row);
            uint32_t source = render_pixel_scale_premultiplied(render_pixel_premultiply_abgr(color.c), alpha);
            uint32_t* destination = impl->pixels + (row + first_row) * (size_t)impl->stride + column + first_column;
            *destination = render_pixel_source_over_premultiplied_pair(*destination, source);
        }
    };
    render_parallel_rows((int)width, (int)height, sample_rows); // INT_CAST_OK: raster dimensions bounded by the caller's integer surface
    return true;
}

void rdt_fill_linear_gradient(RdtVector* vec, RdtPath* path,
                              float x1, float y1, float x2, float y2,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule, const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform, const RdtGradientOptions* options) {
    if (rdt_fill_linear_gradient_analytic(vec, path, x1, y1, x2, y2, stops, stop_count,
            rule, transform, gradient_transform, options)) return;
    float values[4] = {x1, y1, x2, y2};
    tvg_draw_gradient(vec, path, false, values, stops, stop_count, rule,
        transform, gradient_transform, options);
}

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* path,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule, const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform, const RdtGradientOptions* options) {
    float values[3] = {cx, cy, r};
    tvg_draw_gradient(vec, path, true, values, stops, stop_count, rule,
        transform, gradient_transform, options);
}

// ============================================================================
// Clipping
// ============================================================================

// ThorVG has no clip stack. Keep active clip paths until a batch or immediate
// paint is submitted, then compose one alpha mask for that submission.

// Active clip state
#define RDT_INITIAL_CLIP_DEPTH 8

struct ClipEntry {
    RdtPath* path;
    RdtFillRule rule;
    RdtMatrix transform;
    bool has_transform;
};

// We extend RdtVectorImpl with clip state via thread-local storage to avoid
// modifying the struct definition visible to other code.
static thread_local ClipEntry s_clip_inline_stack[RDT_INITIAL_CLIP_DEPTH];
static thread_local ClipEntry* s_clip_stack = s_clip_inline_stack;
static thread_local int s_clip_depth = 0;
static thread_local int s_clip_base = 0;
static thread_local int s_clip_capacity = RDT_INITIAL_CLIP_DEPTH;

static bool ensure_clip_capacity(int needed_depth) {
    if (needed_depth <= s_clip_capacity) return true;
    int new_capacity = s_clip_capacity * 2;
    while (new_capacity < needed_depth) new_capacity *= 2;
    ClipEntry* new_stack = (ClipEntry*)mem_calloc((size_t)new_capacity, sizeof(ClipEntry), MEM_CAT_RENDER); // OBJ_HEAP_OK: thread-local clip stack; replaced on growth, released when empty
    if (!new_stack) {
        log_warn("[RAD_CAP_TVG_CLIP] failed to grow clip stack to depth %d", needed_depth);
        return false;
    }
    if (s_clip_base + s_clip_depth > 0) {
        memcpy(new_stack, s_clip_stack, (size_t)(s_clip_base + s_clip_depth) * sizeof(ClipEntry));
    }
    if (s_clip_stack != s_clip_inline_stack) {
        lam::Temp<ClipEntry> old_stack(s_clip_stack);  // the grown stack replaces the heap one
    }
    s_clip_stack = new_stack;
    s_clip_capacity = new_capacity;
    log_warn("[RAD_CAP_TVG_CLIP] grew clip stack to depth %d", s_clip_capacity);
    return true;
}

static void release_heap_clip_stack_if_empty() {
    if (s_clip_depth != 0 || s_clip_base != 0 || s_clip_stack == s_clip_inline_stack) return;
    lam::Temp<ClipEntry> heap_stack(s_clip_stack);
    s_clip_stack = s_clip_inline_stack;
    s_clip_capacity = RDT_INITIAL_CLIP_DEPTH;
}

void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform, RdtFillRule rule) {
    if (!vec || !vec->impl || !clip_path) return;
    if (!ensure_clip_capacity(s_clip_base + s_clip_depth + 1)) {
        return;
    }

    tvg_finish_clip_segment(vec->impl);

    // clip state pins immutable geometry instead of copying its entries.
    RdtPath* copy = rdt_path_clone(clip_path);
    if (!copy) return;

    ClipEntry* entry = &s_clip_stack[s_clip_base + s_clip_depth++];
    entry->path = copy;
    entry->rule = rule;
    entry->has_transform = (transform != nullptr);
    if (transform) entry->transform = *transform;
}

void rdt_pop_clip(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    if (s_clip_depth <= 0) {
        log_error("rdt_pop_clip: clip stack underflow");
        return;
    }
    tvg_finish_clip_segment(vec->impl);
    s_clip_depth--;
    ClipEntry* entry = &s_clip_stack[s_clip_base + s_clip_depth];
    rdt_path_free(entry->path);
    entry->path = nullptr;
    release_heap_clip_stack_if_empty();
}

int rdt_clip_save_depth() {
    int saved = s_clip_depth;
    // suspended clips keep their owned paths; inner rendering uses a disjoint stack range.
    s_clip_base += saved;
    s_clip_depth = 0;
    return saved;
}

void rdt_clip_restore_depth(int saved_depth) {
    if (saved_depth < 0 || saved_depth > s_clip_base) return;
    for (int i = 0; i < s_clip_depth; i++) {
        ClipEntry* entry = &s_clip_stack[s_clip_base + i];
        rdt_path_free(entry->path); entry->path = nullptr;
    }
    s_clip_base -= saved_depth;
    s_clip_depth = saved_depth;
    release_heap_clip_stack_if_empty();

}

// geometric clips intersect paths without allocating full-surface alpha masks
// for every image batch; nested clippers preserve the clip stack's intersection.
static void apply_clip_masks(RdtVectorImpl* impl, Tvg_Paint shape) {
    if (s_clip_depth <= 0) return;
    RenderMetricTimer timer(&RenderPipelineMetrics::native_clip_ns);
    impl->clip_mask_count++;

    // Build a single composed mask from all clip entries.
    // Start from the outermost clip (index 0) and nest inward.
    Tvg_Paint composed = create_clip_mask(impl, s_clip_stack[s_clip_base].path,
        s_clip_stack[s_clip_base].has_transform ? &s_clip_stack[s_clip_base].transform : nullptr,
        s_clip_stack[s_clip_base].rule);

    for (int i = 1; i < s_clip_depth; i++) {
        ClipEntry* entry = &s_clip_stack[s_clip_base + i];
        if (!entry->path) continue;
        Tvg_Paint inner = create_clip_mask(impl, entry->path,
            entry->has_transform ? &entry->transform : nullptr, entry->rule);
        tvg_paint_set_clip(inner, composed);
        composed = inner;
    }

    tvg_paint_set_clip(shape, composed);
}

// Clip-aware version of tvg_push_draw_remove
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape) {
    if (s_clip_depth > 0 && impl->batch_depth <= 0) {
        apply_clip_masks(impl, shape);
    }
    tvg_push_draw_remove(impl, shape);
}

static bool path_rect_contour_bound(const RdtPath* path, Bound* result) {
    float corners[4][2] = {}, x = 0.0f, y = 0.0f, start_x = 0.0f, start_y = 0.0f;
    size_t count = 0;
    bool started = false, closed = false;
    for (int index = 0; index < path->count; index++) {
        const RdtPath::Entry& entry = path->entries[index];
        if (closed) return false;
        if (entry.cmd == RdtPath::CMD_MOVE) {
            if (started) return false;
            x = start_x = entry.args[0]; y = start_y = entry.args[1]; started = true;
        } else if (!started) return false;
        else if (entry.cmd == RdtPath::CMD_CUBIC) {
            // zero-radius corner arcs contain only repeated endpoints; a curved arc is not a rectangle.
            for (size_t point = 0; point < 3; point++)
                if (entry.args[point * 2] != x || entry.args[point * 2 + 1] != y) return false;
        } else if (entry.cmd == RdtPath::CMD_LINE || entry.cmd == RdtPath::CMD_CLOSE) {
            float next_x = entry.cmd == RdtPath::CMD_CLOSE ? start_x : entry.args[0];
            float next_y = entry.cmd == RdtPath::CMD_CLOSE ? start_y : entry.args[1];
            if (next_x != x || next_y != y) {
                if ((next_x != x && next_y != y) || count == 4) return false;
                corners[count][0] = x; corners[count++][1] = y;
                x = next_x; y = next_y;
            }
            closed = entry.cmd == RdtPath::CMD_CLOSE;
        } else return false;
    }
    if (!closed || count != 4) return false;
    Bound bound = {corners[0][0], corners[0][1], corners[0][0], corners[0][1]};
    for (size_t index = 1; index < 4; index++) {
        bound.left = fminf(bound.left, corners[index][0]); bound.right = fmaxf(bound.right, corners[index][0]);
        bound.top = fminf(bound.top, corners[index][1]); bound.bottom = fmaxf(bound.bottom, corners[index][1]);
    }
    if (bound.left >= bound.right || bound.top >= bound.bottom) return false;
    for (size_t index = 0; index < 4; index++) {
        if ((corners[index][0] != bound.left && corners[index][0] != bound.right) ||
                (corners[index][1] != bound.top && corners[index][1] != bound.bottom)) return false;
        for (size_t previous = 0; previous < index; previous++)
            if (corners[previous][0] == corners[index][0] && corners[previous][1] == corners[index][1]) return false;
    }
    *result = bound;
    return true;
}

bool rdt_path_get_rect_bound(const RdtPath* path, const RdtMatrix* transform, Bound* bound) {
    if (!path || !bound || path->count <= 0) return false;
    const RdtPath::Entry& entry = path->entries[0];
    Bound result;
    if (path->count == 1 && entry.cmd == RdtPath::CMD_RECT) {
        if (entry.args[4] != 0.0f || entry.args[5] != 0.0f || entry.args[2] <= 0.0f || entry.args[3] <= 0.0f) return false;
        result = {entry.args[0], entry.args[1], entry.args[0] + entry.args[2], entry.args[1] + entry.args[3]};
    } else if (!path_rect_contour_bound(path, &result)) return false;
    if (transform && (!rdt_matrix_is_scale_translation(transform) ||
            !rdt_matrix_project_rect_bounds(transform, result.left, result.top, result.right, result.bottom,
                &result.left, &result.top, &result.right, &result.bottom))) return false;
    if (!isfinite(result.left) || !isfinite(result.top) || !isfinite(result.right) || !isfinite(result.bottom)) return false;
    *bound = result;
    return true;
}

static bool vector_raster_clip_rect(RdtVectorImpl* impl, float x, float y, float w, float h, Bound* bounds) {
    float left = x, top = y, right = x + w, bottom = y + h;
    if (!isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom) ||
        !isfinite(impl->tile_offset_x) || !isfinite(impl->tile_offset_y)) return false;
    for (int index = 0; index < s_clip_depth; index++) {
        const ClipEntry& clip = s_clip_stack[s_clip_base + index];
        Bound bound;
        if (!rdt_path_get_rect_bound(clip.path, clip.has_transform ? &clip.transform : nullptr, &bound)) return false;
        left = fmaxf(left, bound.left); top = fmaxf(top, bound.top);
        right = fminf(right, bound.right); bottom = fminf(bottom, bound.bottom);
    }
    left = fmaxf(left - impl->tile_offset_x, 0.0f);
    top = fmaxf(top - impl->tile_offset_y, 0.0f);
    right = fminf(right - impl->tile_offset_x, (float)impl->width);
    bottom = fminf(bottom - impl->tile_offset_y, (float)impl->height);
    if (right <= left || bottom <= top) { *bounds = {}; return true; }
    if (floorf(left) != left || floorf(top) != top || floorf(right) != right || floorf(bottom) != bottom) return false;
    *bounds = {left, top, right, bottom};
    return true;
}

static bool vector_raster_fill_rect(RdtVectorImpl* impl, float x, float y, float w, float h, Color color) {
    if (w <= 0.0f || h <= 0.0f) return false;
    // opaque writes bypass native construction only when they introduce no extra batch barrier.
    if (color.a == 255 && (!impl->opaque_rect_raster_enabled || impl->batch_scene || impl->batch_root)) return false;
    Bound bounds;
    if (!vector_raster_clip_rect(impl, x, y, w, h, &bounds)) return false;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return true;
    // an integral rectangle intersection needs no coverage rasterization; flush before exact source-over on the borrowed target.
    tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_cpu);
    Rect rect = {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
    ImageSurface target = {};
    target.pixels = impl->pixels; target.width = impl->width; target.height = impl->height;
    target.pitch = impl->stride * 4; target.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
    RasterPaintContext raster = raster_paint_context(&target, nullptr, nullptr, 0);
    raster_fill_rect(&raster, &rect, color.c);
    return true;
}

// ============================================================================
// Image drawing
// ============================================================================

static void apply_raster_image_placement(Tvg_Paint picture, int source_width, int source_height,
    float x, float y, float width, float height, const RdtMatrix* transform) {
    // raw raster rectangles can scale independently; picture_set_size preserves aspect ratio.
    RdtMatrix placement = rdt_matrix_translate(x, y);
    RdtMatrix scale = rdt_matrix_identity();
    scale.e11 = width / (float)source_width;
    scale.e22 = height / (float)source_height;
    placement = rdt_matrix_multiply(&placement, &scale);
    if (transform) placement = rdt_matrix_multiply(transform, &placement);
    apply_transform(picture, &placement);
}

static Rect vector_image_projection_viewport(const RdtVectorImpl* impl) {
    Bound visible = {impl->tile_offset_x, impl->tile_offset_y,
        impl->tile_offset_x + impl->width, impl->tile_offset_y + impl->height};
    for (int index = 0; index < s_clip_depth; index++) {
        const ClipEntry& clip = s_clip_stack[s_clip_base + index];
        Bound bound;
        if (!rdt_path_get_bounds(clip.path, &bound.left, &bound.top, &bound.right, &bound.bottom)) continue;
        if (clip.has_transform && !rdt_matrix_project_rect_bounds(&clip.transform,
            bound.left, bound.top, bound.right, bound.bottom,
            &bound.left, &bound.top, &bound.right, &bound.bottom)) continue;
        // keep an antialiasing fringe; the original path still clips the sampled image exactly
        visible.left = fmaxf(visible.left, floorf(bound.left) - 1.0f);
        visible.top = fmaxf(visible.top, floorf(bound.top) - 1.0f);
        visible.right = fminf(visible.right, ceilf(bound.right) + 1.0f);
        visible.bottom = fminf(visible.bottom, ceilf(bound.bottom) + 1.0f);
    }
    return {visible.left, visible.top, fmaxf(0.0f, visible.right - visible.left),
        fmaxf(0.0f, visible.bottom - visible.top)};
}

void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform, uint64_t resource_generation, bool straight_alpha,
                    uint64_t resource_identity, ScaleMode scale_mode) {
    if (!vec || !vec->impl || !pixels) return;
    RdtVectorImpl* impl = vec->impl;
    // A 1:1 image at a whole-pixel translation (a paint layer at rest) maps samples to
    // pixels exactly: composite the source directly under rectangular clips, without
    // resampling it into a projection buffer first.
    float copy_x, copy_y;
    Bound copy_clip;
    // a source inside the target (a canvas drawn onto itself) must go through a separate buffer
    const uint8_t* target_begin = (const uint8_t*)impl->pixels;
    const uint8_t* target_end = target_begin + (size_t)impl->stride * 4 * (size_t)impl->height;
    const uint8_t* source_begin = (const uint8_t*)pixels;
    const uint8_t* source_end = source_begin + (size_t)src_stride * 4 * (size_t)(src_h > 0 ? src_h : 0);
    bool aliased = source_begin < target_end && target_begin < source_end;
    if (!aliased && rdt_image_is_pixel_copy(transform, src_w, src_h, dst_x, dst_y, dst_w, dst_h, &copy_x, &copy_y) &&
        vector_raster_clip_rect(impl, copy_x, copy_y, dst_w, dst_h, &copy_clip)) {
        tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_cpu);
        ImageSurface source = {}, target = {};
        source.pixels = (void*)pixels;
        source.width = src_w; source.height = src_h; source.pitch = src_stride * 4;
        source.alpha_mode = straight_alpha ? IMAGE_ALPHA_STRAIGHT : IMAGE_ALPHA_PREMULTIPLIED;
        target.pixels = impl->pixels; target.width = impl->width; target.height = impl->height;
        target.pitch = impl->stride * 4; target.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
        Rect destination = {copy_x - impl->tile_offset_x, copy_y - impl->tile_offset_y, dst_w, dst_h};
        RasterPaintContext raster = raster_paint_context(&target, &copy_clip, nullptr, 0);
        raster_blit_surface_scaled(&raster, &source, nullptr, &destination, SCALE_MODE_NEAREST, opacity);
        return;
    }
    if (transform || scale_mode != SCALE_MODE_LINEAR) {
        // Use the shared pixel-center sampler for affine and projective images;
        // native picture scaling otherwise disagrees with CPU image blits.
        uint32_t* projected = nullptr;
        Rect rect;
        Rect viewport = vector_image_projection_viewport(impl);
        if (!render_image_project_pixels(pixels, src_w, src_h, src_stride,
            {dst_x, dst_y, dst_w, dst_h}, transform, viewport, scale_mode, straight_alpha, &projected, &rect)) {
            log_error("[IMAGE_PROJECT] Cannot allocate or sample transformed image");
            return;
        }
        lam::Temp<uint32_t> owned_projection(projected);
        if (!projected) return;
        RENDER_METRIC_ADD(projected_image_pixels, (size_t)rect.width * (size_t)rect.height);
        Bound clip;
        if (vector_raster_clip_rect(impl, rect.x, rect.y, rect.width, rect.height, &clip)) {
            // projected samples already own pixel coverage; use the common rounded source-over compositor.
            tvg_flush_batch_scene(impl, &RenderPipelineMetrics::flush_cpu);
            ImageSurface source = {}, target = {};
            source.pixels = projected;
            source.width = (int)rect.width; // INT_CAST_OK: projected buffer pixel columns.
            source.height = (int)rect.height; // INT_CAST_OK: projected buffer pixel rows.
            source.pitch = source.width * 4; source.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
            target.pixels = impl->pixels; target.width = impl->width; target.height = impl->height;
            target.pitch = impl->stride * 4; target.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
            Rect destination = {rect.x - impl->tile_offset_x, rect.y - impl->tile_offset_y, rect.width, rect.height};
            RasterPaintContext raster = raster_paint_context(&target, &clip, nullptr, 0);
            raster_blit_surface_scaled(&raster, &source, nullptr, &destination, SCALE_MODE_NEAREST, opacity);
            return;
        }
        Tvg_Paint picture = tvg_picture_new();
        int width = (int)rect.width, height = (int)rect.height; // INT_CAST_OK: projected buffer pixel dimensions
        if (!picture || tvg_picture_load_raw(picture, projected, width, height,
            TVG_COLORSPACE_ABGR8888, true) != TVG_RESULT_SUCCESS) {
            if (picture) tvg_paint_unref(picture, true);
            log_error("[IMAGE_PROJECT] Cannot upload sampled image");
            return;
        }
        tvg_paint_translate(picture, rect.x, rect.y);
        tvg_paint_set_opacity(picture, opacity);
        tvg_push_draw_remove_clipped(impl, picture);
        return;
    }
    if (src_w <= 0 || src_h <= 0 || src_w > INT_MAX / 4 || src_stride < src_w || src_stride > INT_MAX / 4 ||
        (size_t)src_h > SIZE_MAX / sizeof(uint32_t) / (size_t)src_w) return;
    int tight_stride = src_w * 4;
    // rdt_draw_image receives uint32_t row stride; ThorVG raw upload needs byte rows.
    int src_stride_bytes = src_stride * 4;
    if (src_w <= 0 || src_h <= 0 || src_stride < src_w) return;

    if (resource_generation != 0) {
        tvg_lock(&g_image_paint_cache_mutex);
        Tvg_Paint cached = image_paint_cache_dup_locked(pixels, src_w, src_h, src_stride,
                                                        resource_generation, straight_alpha, resource_identity);
        pthread_mutex_unlock(&g_image_paint_cache_mutex);
        if (cached) {
            apply_raster_image_placement(cached, src_w, src_h, dst_x, dst_y, dst_w, dst_h, transform);
            if (opacity < 255) {
                tvg_paint_set_opacity(cached, opacity);
            }
            tvg_push_draw_remove_clipped(impl, cached);
            return;
        }
    }

    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return;

    const uint32_t* raw_pixels = pixels;
    lam::Temp<uint32_t> tight_pixels;
    if (straight_alpha || src_stride_bytes != tight_stride) {
        // ThorVG raw images have no stride parameter; copy strided rows tightly
        // so clipped/offset image draws cannot make ThorVG read past each row.
        tight_pixels = lam::temp_array<uint32_t>((size_t)src_w * (size_t)src_h, MEM_CAT_IMAGE);
        if (!tight_pixels) {
            tvg_paint_unref(pic, true);
            return;
        }
        const unsigned char* src = (const unsigned char*)pixels;
        unsigned char* dst = (unsigned char*)tight_pixels.get();
        for (int y = 0; y < src_h; y++) {
            const uint32_t* source = (const uint32_t*)(src + (size_t)y * src_stride_bytes);
            uint32_t* target = (uint32_t*)(dst + (size_t)y * tight_stride);
            // convert an owned upload copy; shared decoded images retain their straight-alpha pixels.
            for (int x = 0; x < src_w; x++) {
                uint32_t pixel = source[x];
                target[x] = straight_alpha ? render_pixel_premultiply_abgr(pixel) : pixel;
            }
        }
        raw_pixels = tight_pixels.get();
    }

    // ThorVG copies the tight buffer, so it can be freed when this returns
    bool copy_pixels = (resource_generation != 0 || tight_pixels);
    if (tvg_picture_load_raw(pic, (uint32_t*)raw_pixels, src_w, src_h,
        TVG_COLORSPACE_ABGR8888, copy_pixels) != TVG_RESULT_SUCCESS) {
        log_debug("rdt_draw_image: tvg_picture_load_raw failed");
        tvg_paint_unref(pic, true);
        return;
    }

    if (resource_generation != 0) {
        Tvg_Paint draw = image_paint_cache_store(pixels, src_w, src_h, src_stride,
                                                 resource_generation, straight_alpha, resource_identity, pic);
        if (draw) {
            pic = draw;
        }
    }

    apply_raster_image_placement(pic, src_w, src_h, dst_x, dst_y, dst_w, dst_h, transform);

    if (opacity < 255) {
        tvg_paint_set_opacity(pic, opacity);
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

    Pool* pool = mem_pool_create(mem_context_process(MEM_ROLE_MEDIA), MEM_ROLE_MEDIA, "rdt.vector.tvg");
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

    // picture sources share the inline HTML SVG parser and Element model (D2.11.2).
    lam::Temp<char> buf(mem_dup_n((const char*)data, size, MEM_CAT_RENDER));
    if (!buf) { mem_pool_destroy(pool); return nullptr; }
    Element* svg_root = parse_svg_document(input, buf.get());
    buf.reset();

    if (!input->root.item || input->root.item == ITEM_ERROR) {
        log_error("svg_picture_create: parse_svg_document failed");
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

    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER); // OBJ_HEAP_OK: the caller owns the picture; RdtPicture::release frees it
    p->kind = RdtPicture::KIND_SVG_DOM;
    p->input = input;
    p->pool = pool;
    p->owns_pool = true;
    ref_count_init(&p->ref_count);
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

    tvg_lock(&g_picture_cache_mutex);
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
    lam::Temp<char> buf = lam::temp_array<char>((size_t)fsz, MEM_CAT_RENDER);
    if (!buf) { fclose(fp); return nullptr; }
    size_t rd = fread(buf.get(), 1, (size_t)fsz, fp);
    fclose(fp);

    RdtPicture* p = svg_picture_create(buf.get(), (int)rd, path);
    buf.reset();
    if (!p) {
        log_error("rdt_picture_load: failed to parse %s", path);
        return nullptr;
    }

    tvg_lock(&g_picture_cache_mutex);
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
    tvg_lock(&g_picture_cache_mutex);
    RdtPicture* cached = picture_cache_dup_data_locked(data, size, mime_type, hash);
    pthread_mutex_unlock(&g_picture_cache_mutex);
    if (cached) {
        log_debug("rdt_picture_load_data: cache hit (%d bytes, mime=%s)",
                  size, mime_type ? mime_type : "");
        return cached;
    }

    RdtPicture* p = svg_picture_create(data, size, nullptr);
    if (!p) return nullptr;

    tvg_lock(&g_picture_cache_mutex);
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
        Tvg_Paint dup = tvg_duplicate_paint_locked(paint);
        if (!dup) return nullptr;
        RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER); // OBJ_HEAP_OK: the caller owns the picture; RdtPicture::release frees it
        if (!p) { tvg_paint_unref(dup, true); return nullptr; }
        p->kind = RdtPicture::KIND_TVG_PAINT;
        ref_count_init(&p->ref_count);
        p->paint = dup;
        p->width = width;
        p->height = height;
        p->transform = transform;
        p->has_transform = has_transform;
        return p;
    }
    // SVG_DOM duplicates have independent draw state but retain the parsed
    // owner so cache eviction cannot free the shared Element tree underneath.
    if (!rdt_picture_retain_svg_owner(this)) return nullptr;
    RdtPicture* owner = rdt_picture_svg_owner(this);
    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER); // OBJ_HEAP_OK: the caller owns the picture; RdtPicture::release frees it
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
    p->animation_time = animation_time;
    return p;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    return pic ? pic->dup() : nullptr;
}

double rdt_picture_animation_time(RdtPicture* pic) { return pic ? pic->animation_time : 0; }
void rdt_picture_set_animation_time(RdtPicture* pic, double seconds) {
    if (pic && isfinite(seconds) && seconds >= 0) pic->animation_time = seconds;
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

Element* rdt_picture_get_svg_root(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM) return nullptr;
    return pic->svg_root;
}

Element* rdt_picture_find_svg_element_by_id(RdtPicture* pic, const char* id) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM || !id || !*id) return nullptr;
    return rdt_picture_find_element_id(pic->svg_root, id, rdt_picture_elem_attr);
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

RdtMatrix rdt_picture_compose_transform(RdtPicture* pic, const RdtMatrix* transform) {
    // Compose the explicit per-picture transform (set via set_transform) and
    // the caller-provided transform.  Caller transform applies after.
    RdtMatrix base = rdt_matrix_identity();
    if (pic && pic->has_transform) base = pic->transform;
    if (transform) base = rdt_matrix_multiply(transform, &base);
    return base;
}

bool rdt_picture_is_text(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_TVG_PAINT || !pic->paint) return false;
    Tvg_Type type = TVG_TYPE_UNDEF;
    return tvg_paint_get_type(pic->paint, &type) == TVG_RESULT_SUCCESS && type == TVG_TYPE_TEXT;
}

// Internal helper: render a SVG_DOM picture into vec at the picture's stored
// w/h, applying the optional transform.  No tvg_* calls.
static void svg_dom_picture_draw(RdtVector* vec, RdtPicture* pic,
                                 uint8_t opacity, const RdtMatrix* transform) {
    if (!pic->svg_root) return;
    RdtMatrix base = rdt_picture_compose_transform(pic, transform);
    render_svg_to_vec_via_display_list(vec, pic->svg_root, pic->width, pic->height,
                      pic->pool, 1.0f, g_picture_font_ctx, &base,
                      nullptr, nullptr, pic->source_path,
                      (float)opacity / 255.0f, false, nullptr, true, -1.0f, nullptr, pic->animation_time);
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
    Tvg_Type type = TVG_TYPE_UNDEF;
    // text and shape paints have no Picture loader to resize.
    if (pic->width > 0 && pic->height > 0 &&
        tvg_paint_get_type(paint, &type) == TVG_RESULT_SUCCESS && type == TVG_TYPE_PICTURE) {
        tvg_picture_set_size(paint, pic->width, pic->height);
    }
    if (opacity < 255) tvg_paint_set_opacity(paint, opacity);
    if (pic->has_transform || transform) {
        RdtMatrix placement = rdt_picture_compose_transform(pic, transform);
        apply_transform(paint, &placement);
    }
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
            lam::Temp<RdtPicture> handle(pic);  // a duplicate handle goes; the shared owner may stay
            pic = owner;
        }
    }
    // every picture kind is counted; the last reference frees it
    RefCountRelease released = ref_count_release(&pic->ref_count);
    if (released == REF_COUNT_LIVE) return;
    if (released == REF_COUNT_UNDERFLOW) {
        log_error("rdt_picture_free: picture ref_count underflow");
        return;
    }
    if (pic->kind == RdtPicture::KIND_SVG_DOM) {
        if (pic->pool) mem_pool_destroy(pic->pool);
        lam::Temp<char> source_path(pic->source_path);
        // input is allocated from the pool; destroyed implicitly above
    } else {
        if (pic->paint) tvg_paint_unref(pic->paint, true);
    }
    lam::Temp<RdtPicture> last(pic);  // the last reference frees the picture
}

void rdt_picture_free(RdtPicture* pic) {
    if (pic) pic->release();
}

// ============================================================================
// Bridge: wrap ThorVG paint as RdtPicture (used by render_svg_inline.cpp)
// ============================================================================

RdtPicture* rdt_picture_take_tvg_paint(Tvg_Paint paint, float w, float h) {
    if (!paint) return nullptr;
    RdtPicture* pic = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER); // OBJ_HEAP_OK: the caller owns the picture; RdtPicture::release frees it
    pic->kind = RdtPicture::KIND_TVG_PAINT;
    ref_count_init(&pic->ref_count);
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
        // Release cached pictures before entering context teardown: their
        // private Pools are root-owned, and teardown suppresses unregister
        // hooks, which would otherwise leave freed pool nodes in the root list.
        vector_cache_destroy(&g_vector_cache_owner);
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
