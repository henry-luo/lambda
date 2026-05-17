#pragma once

#include <stdint.h>
#include <stdarg.h>

struct DomNode;
struct LayoutContext;

namespace radiant {

enum LayoutDebugCategory : uint32_t {
    LAYOUT_DEBUG_NONE  = 0,
    LAYOUT_DEBUG_BOX   = 1u << 0,
    LAYOUT_DEBUG_PASS  = 1u << 1,
    LAYOUT_DEBUG_ABS   = 1u << 2,
    LAYOUT_DEBUG_FLEX  = 1u << 3,
    LAYOUT_DEBUG_GRID  = 1u << 4,
    LAYOUT_DEBUG_TABLE = 1u << 5,
    LAYOUT_DEBUG_TEXT  = 1u << 6,
    LAYOUT_DEBUG_CACHE = 1u << 7,
    LAYOUT_DEBUG_ALL   = 0xffffffffu
};

typedef struct LayoutDebugState {
    uint32_t enabled_categories;
    bool initialized;
} LayoutDebugState;

enum LayoutProfileBucket : uint8_t {
    LAYOUT_PROFILE_BLOCK = 0,
    LAYOUT_PROFILE_INLINE,
    LAYOUT_PROFILE_TEXT,
    LAYOUT_PROFILE_FLEX,
    LAYOUT_PROFILE_GRID,
    LAYOUT_PROFILE_TABLE,
    LAYOUT_PROFILE_INTRINSIC,
    LAYOUT_PROFILE_STYLE,
    LAYOUT_PROFILE_IMAGE,
    LAYOUT_PROFILE_BUCKET_COUNT
};

typedef struct LayoutProfileNode {
    const DomNode* node;
    LayoutProfileBucket bucket;
    double elapsed_ms;
} LayoutProfileNode;

typedef struct LayoutProfiler {
    double block_ms;
    double inline_ms;
    double text_ms;
    double flex_ms;
    double grid_ms;
    double table_ms;
    double intrinsic_ms;
    double style_ms;
    double image_ms;
    int64_t cache_hits;
    int64_t cache_misses;

    bool enabled;
    LayoutProfileNode top_nodes[8];
    int top_node_count;
} LayoutProfiler;

void layout_debug_init(LayoutDebugState* state);
bool layout_debug_enabled(LayoutContext* lycon, LayoutDebugCategory category);
void layout_debug_log(LayoutContext* lycon, LayoutDebugCategory category,
                      const DomNode* node, const char* format, ...);
void layout_debug_vlog(LayoutContext* lycon, LayoutDebugCategory category,
                       const DomNode* node, const char* format, va_list args);

void layout_profiler_init(LayoutProfiler* profiler);
void layout_profiler_set_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms);
void layout_profiler_add_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms);
void layout_profiler_record_node(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                 const DomNode* node, double elapsed_ms);
void layout_profiler_note_cache_hit(LayoutProfiler* profiler);
void layout_profiler_note_cache_miss(LayoutProfiler* profiler);
void layout_profiler_set_cache(LayoutProfiler* profiler, int64_t hits, int64_t misses);
void layout_profiler_report(LayoutContext* lycon);
double layout_profiler_now_ms();

struct LayoutProfileScope {
    LayoutContext* lycon;
    const DomNode* node;
    LayoutProfileBucket bucket;
    double start_ms;

    LayoutProfileScope(LayoutContext* l, LayoutProfileBucket b, const DomNode* n);
    ~LayoutProfileScope();

    LayoutProfileScope(const LayoutProfileScope&) = delete;
    LayoutProfileScope& operator=(const LayoutProfileScope&) = delete;
};

} // namespace radiant
