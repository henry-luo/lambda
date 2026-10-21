#include "layout.hpp"
#include "css_resolved_style.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/str.h"
}

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace radiant {

static bool env_enabled(const char* value) {
    if (!value || !value[0]) return false;
    if (strcmp(value, "0") == 0) return false;
    if (strcmp(value, "false") == 0) return false;
    if (strcmp(value, "off") == 0) return false;
    return true;
}

static bool token_equals(const char* token, int len, const char* expected) {
    if (!token || !expected || len < 0) return false;
    return str_ieq_const(token, (size_t)len, expected); // INT_CAST_OK: token length is nonnegative.
}

static uint32_t category_from_token(const char* token, int len) {
    if (token_equals(token, len, "all")) return LAYOUT_DEBUG_ALL;
    if (token_equals(token, len, "box") || token_equals(token, len, "layout_box")) return LAYOUT_DEBUG_BOX;
    if (token_equals(token, len, "pass") || token_equals(token, len, "layout_pass")) return LAYOUT_DEBUG_PASS;
    if (token_equals(token, len, "abs") || token_equals(token, len, "layout_abs")) return LAYOUT_DEBUG_ABS;
    if (token_equals(token, len, "flex") || token_equals(token, len, "layout_flex")) return LAYOUT_DEBUG_FLEX;
    if (token_equals(token, len, "grid") || token_equals(token, len, "layout_grid")) return LAYOUT_DEBUG_GRID;
    if (token_equals(token, len, "table") || token_equals(token, len, "layout_table")) return LAYOUT_DEBUG_TABLE;
    if (token_equals(token, len, "text") || token_equals(token, len, "layout_text")) return LAYOUT_DEBUG_TEXT;
    if (token_equals(token, len, "cache") || token_equals(token, len, "layout_cache")) return LAYOUT_DEBUG_CACHE;
    return LAYOUT_DEBUG_NONE;
}

static const char* bucket_name(LayoutProfileBucket bucket) {
    switch (bucket) {
        case LAYOUT_PROFILE_BLOCK: return "block";
        case LAYOUT_PROFILE_INLINE: return "inline";
        case LAYOUT_PROFILE_TEXT: return "text";
        case LAYOUT_PROFILE_FLEX: return "flex";
        case LAYOUT_PROFILE_GRID: return "grid";
        case LAYOUT_PROFILE_TABLE: return "table";
        case LAYOUT_PROFILE_INTRINSIC: return "intrinsic";
        case LAYOUT_PROFILE_STYLE: return "style";
        case LAYOUT_PROFILE_IMAGE: return "image";
        case LAYOUT_PROFILE_INTRINSIC_HEIGHT: return "intrinsic-height";
        case LAYOUT_PROFILE_FLEX_PREPARE: return "flex-prepare";
        case LAYOUT_PROFILE_FLEX_BASIS: return "flex-basis";
        case LAYOUT_PROFILE_FLEX_CROSS: return "flex-cross";
        case LAYOUT_PROFILE_FLEX_CONTENT: return "flex-content";
        default: return "unknown";
    }
}

void layout_debug_init(LayoutDebugState* state) {
    if (!state) return;
    state->enabled_categories = LAYOUT_DEBUG_NONE;
    state->initialized = true;

    const char* env = getenv("LAYOUT_DEBUG");
    if (!env_enabled(env)) return;

    const char* token = env;
    int len = 0;
    for (const char* p = env; ; p++) {
        char ch = *p;
        bool is_sep = (ch == '\0' || ch == ',' || ch == ';' || isspace((unsigned char)ch));
        if (is_sep) {
            if (len > 0) {
                state->enabled_categories |= category_from_token(token, len);
            }
            if (ch == '\0') break;
            token = p + 1;
            len = 0;
        } else {
            len++;
        }
    }
}

bool layout_debug_enabled(LayoutContext* lycon, LayoutDebugCategory category) {
    if (!lycon) return false;
    if (!lycon->layout_debug.initialized) {
        layout_debug_init(&lycon->layout_debug);
    }
    return (lycon->layout_debug.enabled_categories & (uint32_t)category) != 0;
}

void layout_debug_vlog(LayoutContext* lycon, LayoutDebugCategory category,
                       const DomNode* node, const char* format, va_list args) {
    if (!layout_debug_enabled(lycon, category) || !format) return;

    static thread_local char message[1024];  // LARGE_ARRAY_OK: static buffer — not on call stack.
    vsnprintf(message, sizeof(message), format, args);
}

void layout_debug_log(LayoutContext* lycon, LayoutDebugCategory category,
                      const DomNode* node, const char* format, ...) {
    va_list args;
    va_start(args, format);
    layout_debug_vlog(lycon, category, node, format, args);
    va_end(args);
}

double layout_profiler_now_ms() {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static BOOL has_freq = QueryPerformanceFrequency(&freq);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return has_freq ? ((double)counter.QuadPart * 1000.0 / (double)freq.QuadPart) : 0.0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

void layout_profiler_init(LayoutProfiler* profiler) {
    if (!profiler) return;
    memset(profiler, 0, sizeof(LayoutProfiler));
    profiler->enabled = env_enabled(getenv("LAYOUT_PROFILE"));
}

static double* bucket_ptr(LayoutProfiler* profiler, LayoutProfileBucket bucket) {
    if (!profiler) return nullptr;
    switch (bucket) {
        case LAYOUT_PROFILE_BLOCK: return &profiler->block_ms;
        case LAYOUT_PROFILE_INLINE: return &profiler->inline_ms;
        case LAYOUT_PROFILE_TEXT: return &profiler->text_ms;
        case LAYOUT_PROFILE_FLEX: return &profiler->flex_ms;
        case LAYOUT_PROFILE_GRID: return &profiler->grid_ms;
        case LAYOUT_PROFILE_TABLE: return &profiler->table_ms;
        case LAYOUT_PROFILE_INTRINSIC: return &profiler->intrinsic_ms;
        case LAYOUT_PROFILE_STYLE: return &profiler->style_ms;
        case LAYOUT_PROFILE_IMAGE: return &profiler->image_ms;
        default: return nullptr;
    }
}

void layout_profiler_set_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms) {
    double* slot = bucket_ptr(profiler, bucket);
    if (slot) *slot = elapsed_ms;
}

void layout_profiler_add_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms) {
    double* slot = bucket_ptr(profiler, bucket);
    if (slot) *slot += elapsed_ms;
}

void layout_profiler_record_node(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                 const DomNode* node, double elapsed_ms) {
    if (!profiler || !profiler->enabled || !node || elapsed_ms <= 0.0) return;

    int insert_at = profiler->top_node_count;
    if (insert_at < 8) {
        profiler->top_node_count++;
    } else if (elapsed_ms <= profiler->top_nodes[7].elapsed_ms) {
        return;
    } else {
        insert_at = 7;
    }

    while (insert_at > 0 && profiler->top_nodes[insert_at - 1].elapsed_ms < elapsed_ms) {
        profiler->top_nodes[insert_at] = profiler->top_nodes[insert_at - 1];
        insert_at--;
    }
    profiler->top_nodes[insert_at].node = lam::up(node);
    profiler->top_nodes[insert_at].bucket = bucket;
    profiler->top_nodes[insert_at].elapsed_ms = elapsed_ms;
}

void layout_profiler_note_cache_hit(LayoutProfiler* profiler) {
    if (profiler) profiler->cache_hits++;
}

void layout_profiler_note_cache_miss(LayoutProfiler* profiler) {
    if (profiler) profiler->cache_misses++;
}

void layout_profiler_note_intrinsic_request(LayoutProfiler* profiler, bool cache_hit,
                                            bool reentrant) {
    if (!profiler) return;
    profiler->intrinsic_requests++;
    if (cache_hit) profiler->intrinsic_cache_hits++;
    else profiler->intrinsic_cache_misses++;
    if (reentrant) profiler->intrinsic_reentrant++;
}

void layout_profiler_set_cache(LayoutProfiler* profiler, int64_t hits, int64_t misses) {
    if (!profiler) return;
    profiler->cache_hits = hits;
    profiler->cache_misses = misses;
}

void layout_profiler_report(LayoutContext* lycon) {
    if (!lycon || !lycon->profiler.enabled) return;
    LayoutProfiler* p = &lycon->profiler;

    for (uint8_t bucket = 0; bucket < LAYOUT_PROFILE_BUCKET_COUNT; bucket++) {
        if (!p->scope_calls[bucket]) continue;
        log_notice("[LAYOUT_PROFILE] scope: %s calls=%llu inclusive=%.1fms self=%.1fms",
            bucket_name(static_cast<LayoutProfileBucket>(bucket)),
            (unsigned long long)p->scope_calls[bucket], p->scope_total_ms[bucket],
            p->scope_self_ms[bucket]);
    }
    log_notice("[LAYOUT_PROFILE] intrinsic_miss: cold=%llu invalidated=%llu generation=%llu query=%llu block=%llu parent=%llu",
        (unsigned long long)p->intrinsic_miss_reasons[0],
        (unsigned long long)p->intrinsic_miss_reasons[1],
        (unsigned long long)p->intrinsic_miss_reasons[2],
        (unsigned long long)p->intrinsic_miss_reasons[3],
        (unsigned long long)p->intrinsic_miss_reasons[4],
        (unsigned long long)p->intrinsic_miss_reasons[5]);
    log_notice("[LAYOUT_PROFILE] height: requests=%llu hits=%llu computations=%llu uncacheable=%llu",
        (unsigned long long)p->height_requests, (unsigned long long)p->height_cache_hits,
        (unsigned long long)p->height_computations, (unsigned long long)p->height_uncacheable);
    log_notice("[LAYOUT_PROFILE] height-cache: stores=%llu unstable=%llu version=%llu available=%llu geometry=%llu font=%llu width=%llu environment=%llu policy=%llu",
        (unsigned long long)p->height_stores, (unsigned long long)p->height_unstable,
        (unsigned long long)p->height_version_misses,
        (unsigned long long)p->height_key_misses[0], (unsigned long long)p->height_key_misses[1],
        (unsigned long long)p->height_key_misses[2], (unsigned long long)p->height_key_misses[3],
        (unsigned long long)p->height_key_misses[4], (unsigned long long)p->height_key_misses[5]);

    log_notice("[LAYOUT_PROFILE] buckets: block=%.1fms inline=%.1fms text=%.1fms flex=%.1fms grid=%.1fms table=%.1fms intrinsic=%.1fms style=%.1fms image=%.1fms cache=%lld/%lld",
        p->block_ms, p->inline_ms, p->text_ms, p->flex_ms, p->grid_ms, p->table_ms,
        p->intrinsic_ms, p->style_ms, p->image_ms,
        (long long)p->cache_hits, (long long)p->cache_misses);
    log_notice("[LAYOUT_PROFILE] intrinsic: requests=%llu hits=%llu misses=%llu reentrant=%llu inclusive=%.1fms exclusive=%.1fms",
        (unsigned long long)p->intrinsic_requests,
        (unsigned long long)p->intrinsic_cache_hits,
        (unsigned long long)p->intrinsic_cache_misses,
        (unsigned long long)p->intrinsic_reentrant,
        p->intrinsic_inclusive_ms, p->intrinsic_exclusive_ms);
    log_notice("[LAYOUT_PROFILE] css: full=%lld measure=%lld reuse=%lld geometry=%lld",
        (long long)g_style_resolve_full, (long long)g_style_resolve_measure,
        (long long)g_style_resolve_incremental, (long long)g_style_geometry_evaluations);
    for (uint8_t reason = 0; reason < CSS_STYLE_MISS_COUNT; reason++) {
        if (css_style_misses[reason]) log_notice("[LAYOUT_PROFILE] css_miss: %s=%llu",
            css_style_miss_name(static_cast<CssStyleMiss>(reason)),
            (unsigned long long)css_style_misses[reason]);
    }
    log_notice("[LAYOUT_PROFILE] shifted_reuse: %lld flex_retained: %lld",
        (long long)g_layout_shifted_reuse_count, (long long)g_layout_flex_retained_count);
    log_notice("[LAYOUT_PROFILE] completed-flex-flow: measured=%llu reused=%llu",
        (unsigned long long)p->flex_flow_measures, (unsigned long long)p->flex_flow_reuses);

    for (int i = 0; i < p->top_node_count; i++) {
        const LayoutProfileNode* entry = &p->top_nodes[i];
        log_notice("[LAYOUT_PROFILE] top[%d] %s %.1fms %s",
            i + 1, bucket_name(entry->bucket), entry->elapsed_ms,
            entry->node ? entry->node->source_loc() : "-");
    }
}

void layout_restore_context(LayoutContext* context, const LayoutContext& saved) {
    // restoring geometry must not erase the work performed by nested layout.
    LayoutProfiler profiler = context->profiler;
    *context = saved;
    context->profiler = profiler;
}

thread_local LayoutProfileScope* LayoutProfileScope::active = nullptr;

LayoutProfileScope::LayoutProfileScope(LayoutContext* l, LayoutProfileBucket b, const DomNode* n)
    : lycon(l), node(n), bucket(b), start_ms(0.0), child_ms(0.0), parent(nullptr) {
    if (lycon && lycon->profiler.enabled) {
        parent = active;
        active = this;
        lycon->profiler.scope_calls[bucket]++;
        start_ms = layout_profiler_now_ms();
    }
}

LayoutProfileScope::~LayoutProfileScope() {
    if (!lycon || !lycon->profiler.enabled || start_ms <= 0.0) return;
    double elapsed = layout_profiler_now_ms() - start_ms;
    if (parent) parent->child_ms += elapsed;
    active = parent;
    lycon->profiler.scope_total_ms[bucket] += elapsed;
    lycon->profiler.scope_self_ms[bucket] += elapsed - child_ms;
    layout_profiler_add_bucket(&lycon->profiler, bucket, elapsed);
    layout_profiler_record_node(&lycon->profiler, bucket, node, elapsed);
    if (elapsed >= 100.0 && node) {
        log_notice("[LAYOUT_PROFILE] subtree: %s %.1fms self=%.1fms %s",
            bucket_name(bucket), elapsed, elapsed - child_ms, node->source_loc());
    }
}

} // namespace radiant
