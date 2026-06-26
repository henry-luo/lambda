#include "layout_debug.hpp"
#include "layout.hpp"

extern "C" {
#include "../lib/log.h"
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
    int expected_len = (int)strlen(expected); // INT_CAST_OK: token length comparison
    if (len != expected_len) return false;
    for (int i = 0; i < len; i++) {
        if ((char)tolower(token[i]) != expected[i]) return false;
    }
    return true;
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

#ifndef NDEBUG
static const char* category_name(LayoutDebugCategory category) {
    switch (category) {
        case LAYOUT_DEBUG_BOX: return "LAYOUT_BOX";
        case LAYOUT_DEBUG_PASS: return "LAYOUT_PASS";
        case LAYOUT_DEBUG_ABS: return "LAYOUT_ABS";
        case LAYOUT_DEBUG_FLEX: return "LAYOUT_FLEX";
        case LAYOUT_DEBUG_GRID: return "LAYOUT_GRID";
        case LAYOUT_DEBUG_TABLE: return "LAYOUT_TABLE";
        case LAYOUT_DEBUG_TEXT: return "LAYOUT_TEXT";
        case LAYOUT_DEBUG_CACHE: return "LAYOUT_CACHE";
        default: return "LAYOUT";
    }
}
#endif

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
    log_debug("[%s] %s %s", category_name(category),
              node ? node->source_loc() : "-", message);
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
    profiler->top_nodes[insert_at].node = node;
    profiler->top_nodes[insert_at].bucket = bucket;
    profiler->top_nodes[insert_at].elapsed_ms = elapsed_ms;
}

void layout_profiler_note_cache_hit(LayoutProfiler* profiler) {
    if (profiler) profiler->cache_hits++;
}

void layout_profiler_note_cache_miss(LayoutProfiler* profiler) {
    if (profiler) profiler->cache_misses++;
}

void layout_profiler_set_cache(LayoutProfiler* profiler, int64_t hits, int64_t misses) {
    if (!profiler) return;
    profiler->cache_hits = hits;
    profiler->cache_misses = misses;
}

void layout_profiler_report(LayoutContext* lycon) {
    if (!lycon || !lycon->profiler.enabled) return;
    LayoutProfiler* p = &lycon->profiler;

    log_notice("[LAYOUT_PROFILE] buckets: block=%.1fms inline=%.1fms text=%.1fms flex=%.1fms grid=%.1fms table=%.1fms intrinsic=%.1fms style=%.1fms image=%.1fms cache=%lld/%lld",
        p->block_ms, p->inline_ms, p->text_ms, p->flex_ms, p->grid_ms, p->table_ms,
        p->intrinsic_ms, p->style_ms, p->image_ms,
        (long long)p->cache_hits, (long long)p->cache_misses);

    for (int i = 0; i < p->top_node_count; i++) {
        const LayoutProfileNode* entry = &p->top_nodes[i];
        log_notice("[LAYOUT_PROFILE] top[%d] %s %.1fms %s",
            i + 1, bucket_name(entry->bucket), entry->elapsed_ms,
            entry->node ? entry->node->source_loc() : "-");
    }
}

LayoutProfileScope::LayoutProfileScope(LayoutContext* l, LayoutProfileBucket b, const DomNode* n)
    : lycon(l), node(n), bucket(b), start_ms(0.0) {
    if (lycon && lycon->profiler.enabled) {
        start_ms = layout_profiler_now_ms();
    }
}

LayoutProfileScope::~LayoutProfileScope() {
    if (!lycon || !lycon->profiler.enabled || start_ms <= 0.0) return;
    double elapsed = layout_profiler_now_ms() - start_ms;
    layout_profiler_add_bucket(&lycon->profiler, bucket, elapsed);
    layout_profiler_record_node(&lycon->profiler, bucket, node, elapsed);
}

} // namespace radiant
