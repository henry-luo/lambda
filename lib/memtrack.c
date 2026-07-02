/**
 * Lambda Memory Tracker Implementation
 *
 * A lightweight memory tracking system with:
 * - Category-based allocation tracking
 * - Debug mode with guard bytes and leak detection
 * - Memory pressure callbacks for runtime management
 */

#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "hashmap.h"
#include "arraylist.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>

// ============================================================================
// Configuration
// ============================================================================

// Guard byte patterns for buffer overflow detection
#define GUARD_BYTE_HEAD     0xDE

// Inline header for STATS mode — stores size, category and source line for mem_free
#define MEMTRACK_STATS_MAGIC  0xBEEF

typedef struct MemAllocHeader {
    uint32_t size;          // user-requested size (up to 4 GB)
    uint16_t category;      // MemCategory
    uint16_t magic;         // MEMTRACK_STATS_MAGIC — validates tracked allocation
    int line;               // allocation source line
    uint32_t reserved;      // keeps user pointer aligned after the header
} MemAllocHeader;
#define GUARD_BYTE_TAIL     0xAD
#define GUARD_SIZE          16          // Bytes of guards on each side
#define FILL_BYTE_ALLOC     0xCD        // Fill pattern for new allocations
#define FILL_BYTE_FREE      0xDD        // Fill pattern for freed memory

// Maximum tracked allocations (for debug mode hashmap)
#define MAX_TRACKED_ALLOCS  (1024 * 1024)

// Maximum pressure callbacks
#define MAX_PRESSURE_CALLBACKS 32

// Maximum snapshots
#define MAX_SNAPSHOTS 16

// Maximum aggregate allocation-site stats tracked in STATS mode
#define MAX_LINE_STATS 4096
#define MAX_LINE_STATS_REPORT 100

// ============================================================================
// Category Names
// ============================================================================

const char* memtrack_category_names[MEM_CAT_COUNT] = {
    [MEM_CAT_UNKNOWN]      = "unknown",
    [MEM_CAT_AST]          = "ast",
    [MEM_CAT_PARSER]       = "parser",
    [MEM_CAT_EVAL]         = "eval",
    [MEM_CAT_STRING]       = "string",
    [MEM_CAT_CONTAINER]    = "container",
    [MEM_CAT_NAMEPOOL]     = "namepool",
    [MEM_CAT_SHAPEPOOL]    = "shapepool",
    [MEM_CAT_INPUT_JSON]   = "input-json",
    [MEM_CAT_INPUT_XML]    = "input-xml",
    [MEM_CAT_INPUT_HTML]   = "input-html",
    [MEM_CAT_INPUT_CSS]    = "input-css",
    [MEM_CAT_INPUT_MD]     = "input-md",
    [MEM_CAT_INPUT_PDF]    = "input-pdf",
    [MEM_CAT_INPUT_INI]    = "input-ini",
    [MEM_CAT_INPUT_YAML]   = "input-yaml",
    [MEM_CAT_INPUT_TOML]   = "input-toml",
    [MEM_CAT_INPUT_CSV]    = "input-csv",
    [MEM_CAT_INPUT_PROPS]  = "input-props",
    [MEM_CAT_INPUT_MARKUP] = "input-markup",
    [MEM_CAT_INPUT_ORG]    = "input-org",
    [MEM_CAT_INPUT_ADOC]   = "input-adoc",
    [MEM_CAT_INPUT_MDX]    = "input-mdx",
    [MEM_CAT_INPUT_MATH]   = "input-math",
    [MEM_CAT_INPUT_OTHER]  = "input-other",
    [MEM_CAT_FORMAT]       = "format",
    [MEM_CAT_DOM]          = "dom",
    [MEM_CAT_LAYOUT]       = "layout",
    [MEM_CAT_STYLE]        = "style",
    [MEM_CAT_FONT]         = "font",
    [MEM_CAT_IMAGE]        = "image",
    [MEM_CAT_RENDER]       = "render",
    [MEM_CAT_CACHE_FONT]   = "cache-font",
    [MEM_CAT_CACHE_IMAGE]  = "cache-image",
    [MEM_CAT_CACHE_LAYOUT] = "cache-layout",
    [MEM_CAT_CACHE_OTHER]  = "cache-other",
    [MEM_CAT_JS_RUNTIME]   = "js-runtime",
    [MEM_CAT_PY_RUNTIME]   = "py-runtime",
    [MEM_CAT_RB_RUNTIME]   = "rb-runtime",
    [MEM_CAT_BASH_RUNTIME] = "bash-runtime",
    [MEM_CAT_NETWORK]      = "network",
    [MEM_CAT_SERVE]        = "serve",
    [MEM_CAT_SYSTEM]       = "system",
    [MEM_CAT_TEMP]         = "temp",
};

// ============================================================================
// Internal Structures
// ============================================================================

// Allocation record (debug mode only)
typedef struct AllocInfo {
    void* ptr;                  // User pointer (key for hashmap)
    void* real_ptr;             // Actual allocation (with guards)
    size_t size;                // User-requested size
    size_t real_size;           // Actual allocated size (with guards)
    MemCategory category;
    uint64_t alloc_id;          // Monotonic allocation ID
    int line;
} AllocInfo;

// Pressure callback entry
typedef struct PressureCallbackEntry {
    MemPressureCallback callback;
    void* user_data;
    uint64_t categories;        // Bitmask of categories this can free
    uint32_t handle;
    bool active;
} PressureCallbackEntry;

// Snapshot entry
typedef struct SnapshotEntry {
    uint32_t handle;
    bool active;
    MemtrackStats stats;
} SnapshotEntry;

// Aggregate allocation-site stats for STATS mode
typedef struct MemtrackLineStats {
    bool used;
    MemCategory category;
    int line;
    size_t current_bytes;
    size_t current_count;
    size_t total_allocs;
} MemtrackLineStats;

// Global tracker state
typedef struct MemtrackState {
    MemtrackMode mode;
    bool initialized;

    // Statistics (always maintained)
    MemtrackStats stats;
    MemtrackLineStats line_stats[MAX_LINE_STATS];
    size_t line_stats_overflow_count;
    size_t line_stats_overflow_bytes;

    // Lock for thread safety
    pthread_mutex_t lock;

    // Allocation registry (debug mode only)
    HashMap* alloc_map;         // ptr -> AllocInfo*
    uint64_t next_alloc_id;

    // Pressure management
    size_t soft_limit;
    size_t hard_limit;
    size_t critical_limit;
    PressureCallbackEntry pressure_callbacks[MAX_PRESSURE_CALLBACKS];
    uint32_t next_callback_handle;

    // Snapshots
    SnapshotEntry snapshots[MAX_SNAPSHOTS];
    uint32_t next_snapshot_handle;

    // Pool/Arena lifecycle counters
    int32_t pool_count;
    int32_t arena_count;

} MemtrackState;

// Global state
static MemtrackState g_memtrack = {0};

// Thread-local tracking enable flag
static __thread bool tls_tracking_enabled = true;

// ============================================================================
// Internal Helpers
// ============================================================================

static void memtrack_console_report(const char* level, const char* fmt, ...) {
    char body[1024];
    va_list args;
    va_start(args, fmt);
    int body_len = vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    if (body_len < 0) {
        return;
    }
    if (body_len >= (int)sizeof(body)) {
        body_len = (int)sizeof(body) - 1;
        body[body_len] = '\0';
    }

    char line[1200];
    int line_len = snprintf(line, sizeof(line), "memtrack-console: %s %s\n", level, body);
    if (line_len < 0) {
        return;
    }
    if (line_len >= (int)sizeof(line)) {
        line_len = (int)sizeof(line) - 1;
        line[line_len - 1] = '\n';
        line[line_len] = '\0';
    }

    ssize_t ignored = write(STDERR_FILENO, line, (size_t)line_len);
    (void)ignored;
}

#define memtrack_report_error(...) \
    do { \
        log_error(__VA_ARGS__); \
        memtrack_console_report("ERR!", __VA_ARGS__); \
    } while (0)

#define memtrack_report_warn(...) \
    do { \
        log_warn(__VA_ARGS__); \
        memtrack_console_report("WARN", __VA_ARGS__); \
    } while (0)

static inline void lock_tracker(void) {
    pthread_mutex_lock(&g_memtrack.lock);
}

static inline void unlock_tracker(void) {
    pthread_mutex_unlock(&g_memtrack.lock);
}

static uint64_t hash_ptr(const void* item, uint64_t seed0, uint64_t seed1) {
    // Item is a pointer to AllocInfo, we want to hash the pointer field
    const AllocInfo* info = (const AllocInfo*)item;
    (void)seed0; (void)seed1;
    return (uint64_t)info->ptr * 0x9E3779B97F4A7C15ULL;
}

static int cmp_ptr(const void* a, const void* b, void* udata) {
    const AllocInfo* ia = (const AllocInfo*)a;
    const AllocInfo* ib = (const AllocInfo*)b;
    (void)udata;
    if (ia->ptr == ib->ptr) return 0;
    return (ia->ptr < ib->ptr) ? -1 : 1;
}

static MemPressureLevel compute_pressure_level(size_t current_bytes) {
    if (g_memtrack.critical_limit && current_bytes >= g_memtrack.critical_limit) {
        return MEM_PRESSURE_CRITICAL;
    }
    if (g_memtrack.hard_limit && current_bytes >= g_memtrack.hard_limit) {
        return MEM_PRESSURE_HIGH;
    }
    if (g_memtrack.soft_limit && current_bytes >= g_memtrack.soft_limit) {
        return MEM_PRESSURE_LOW;
    }
    return MEM_PRESSURE_NONE;
}

static void update_category_stats_alloc(MemCategory cat, size_t size) {
    MemtrackCategoryStats* cs = &g_memtrack.stats.categories[cat];
    cs->current_bytes += size;
    cs->current_count++;
    cs->total_allocs++;
    cs->total_bytes_alloc += size;

    if (cs->current_bytes > cs->peak_bytes) {
        cs->peak_bytes = cs->current_bytes;
    }
    if (cs->current_count > cs->peak_count) {
        cs->peak_count = cs->current_count;
    }
}

static void update_category_stats_free(MemCategory cat, size_t size) {
    MemtrackCategoryStats* cs = &g_memtrack.stats.categories[cat];
    cs->current_bytes -= size;
    cs->current_count--;
    cs->total_frees++;
}

static void update_global_stats_alloc(size_t size) {
    g_memtrack.stats.current_bytes += size;
    g_memtrack.stats.current_count++;
    g_memtrack.stats.total_allocs++;

    if (g_memtrack.stats.current_bytes > g_memtrack.stats.peak_bytes) {
        g_memtrack.stats.peak_bytes = g_memtrack.stats.current_bytes;
    }
    if (g_memtrack.stats.current_count > g_memtrack.stats.peak_count) {
        g_memtrack.stats.peak_count = g_memtrack.stats.current_count;
    }
}

static void update_global_stats_free(size_t size) {
    g_memtrack.stats.current_bytes -= size;
    g_memtrack.stats.current_count--;
    g_memtrack.stats.total_frees++;
}

static size_t line_stats_hash(MemCategory category, int line) {
    uint32_t hash = (uint32_t)line;
    hash ^= (uint32_t)category * 16777619u;
    hash *= 2166136261u;
    return (size_t)(hash % MAX_LINE_STATS);
}

static MemtrackLineStats* find_line_stats_slot(MemCategory category, int line, bool create) {
    if (line <= 0) {
        return NULL;
    }

    size_t index = line_stats_hash(category, line);
    for (size_t probe = 0; probe < MAX_LINE_STATS; probe++) {
        MemtrackLineStats* slot = &g_memtrack.line_stats[(index + probe) % MAX_LINE_STATS];
        if (!slot->used) {
            if (!create) {
                return NULL;
            }
            slot->used = true;
            slot->category = category;
            slot->line = line;
            return slot;
        }
        if (slot->category == category && slot->line == line) {
            return slot;
        }
    }
    return NULL;
}

static void update_line_stats_alloc(MemCategory category, size_t size, int line) {
    MemtrackLineStats* slot = find_line_stats_slot(category, line, true);
    if (!slot) {
        g_memtrack.line_stats_overflow_count++;
        g_memtrack.line_stats_overflow_bytes += size;
        return;
    }
    slot->current_bytes += size;
    slot->current_count++;
    slot->total_allocs++;
}

static void update_line_stats_free(MemCategory category, size_t size, int line) {
    MemtrackLineStats* slot = find_line_stats_slot(category, line, false);
    if (!slot) {
        if (g_memtrack.line_stats_overflow_count > 0) {
            g_memtrack.line_stats_overflow_count--;
        }
        if (g_memtrack.line_stats_overflow_bytes >= size) {
            g_memtrack.line_stats_overflow_bytes -= size;
        } else {
            g_memtrack.line_stats_overflow_bytes = 0;
        }
        return;
    }
    if (slot->current_count > 0) {
        slot->current_count--;
    }
    if (slot->current_bytes >= size) {
        slot->current_bytes -= size;
    } else {
        slot->current_bytes = 0;
    }
}

static void fill_guard_bytes(void* ptr, size_t size, uint8_t pattern) {
    memset(ptr, pattern, size);
}

static bool verify_guard_bytes(void* ptr, size_t size, uint8_t expected) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != expected) {
            return false;
        }
    }
    return true;
}

static void trigger_pressure_callbacks(MemPressureLevel level, size_t target) {
    for (int i = 0; i < MAX_PRESSURE_CALLBACKS; i++) {
        PressureCallbackEntry* entry = &g_memtrack.pressure_callbacks[i];
        if (entry->active && entry->callback) {
            // Unlock during callback to prevent deadlock
            unlock_tracker();
            size_t freed = entry->callback(level, target, entry->user_data);
            lock_tracker();

            if (freed > 0) {
                log_debug("memtrack: pressure callback freed %zu bytes", freed);
            }
        }
    }
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool memtrack_init(MemtrackMode mode) {
    if (g_memtrack.initialized) {
        memtrack_report_warn("memtrack: already initialized");
        return true;
    }

    memset(&g_memtrack, 0, sizeof(g_memtrack));
    pthread_mutex_init(&g_memtrack.lock, NULL);

    g_memtrack.mode = mode;
    g_memtrack.next_alloc_id = 1;
    g_memtrack.next_callback_handle = 1;
    g_memtrack.next_snapshot_handle = 1;

    // Set default limits (can be overridden)
    g_memtrack.soft_limit = 256 * 1024 * 1024;      // 256 MB
    g_memtrack.hard_limit = 512 * 1024 * 1024;      // 512 MB
    g_memtrack.critical_limit = 768 * 1024 * 1024;  // 768 MB

    if (mode == MEMTRACK_MODE_DEBUG) {
        // Use raw malloc/realloc/free to avoid tracking the hashmap itself
        g_memtrack.alloc_map = hashmap_new_with_allocator(
            malloc, realloc, free,  // Use raw allocators
            sizeof(AllocInfo),    // Element size
            MAX_TRACKED_ALLOCS,  // Initial capacity
            0, 0,                // Random seeds
            hash_ptr,            // Hash function
            cmp_ptr,             // Compare function
            NULL,                // No element free function
            NULL                 // No user data
        );
        if (!g_memtrack.alloc_map) {
            memtrack_report_error("memtrack: failed to create allocation map");
            return false;
        }
    }

    g_memtrack.initialized = true;
    log_info("memtrack: initialized in %s mode",
             mode == MEMTRACK_MODE_OFF ? "OFF" :
             mode == MEMTRACK_MODE_STATS ? "STATS" : "DEBUG");

    return true;
}

static void memtrack_log_live_line_stats(void) {
    size_t shown = 0;
    size_t hidden = 0;

    for (int category = 0; category < MEM_CAT_COUNT; category++) {
        for (size_t i = 0; i < MAX_LINE_STATS; i++) {
            MemtrackLineStats* slot = &g_memtrack.line_stats[i];
            if (!slot->used || slot->current_count == 0 ||
                    slot->category != (MemCategory)category) {
                continue;
            }
            if (shown < MAX_LINE_STATS_REPORT) {
                memtrack_report_error("memtrack:     %s line %d: %zu allocs, %zu bytes",
                        memtrack_category_names[slot->category], slot->line,
                        slot->current_count, slot->current_bytes);
                shown++;
            } else {
                hidden++;
            }
        }
    }

    if (g_memtrack.line_stats_overflow_count > 0) {
        memtrack_report_error("memtrack:     unknown line: %zu allocs, %zu bytes",
                g_memtrack.line_stats_overflow_count,
                g_memtrack.line_stats_overflow_bytes);
    }
    if (hidden > 0) {
        memtrack_report_error("memtrack:     ... %zu more allocation line(s)", hidden);
    }
}

size_t memtrack_shutdown(void) {
    if (!g_memtrack.initialized) {
        return 0;
    }

    lock_tracker();

    // Capture leak count while holding lock
    size_t leak_count = 0;
    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG && g_memtrack.alloc_map) {
        leak_count = hashmap_count(g_memtrack.alloc_map);
    } else if (g_memtrack.mode == MEMTRACK_MODE_STATS) {
        leak_count = g_memtrack.stats.current_count;
    }

    // Capture stats while holding lock
    size_t peak_bytes = g_memtrack.stats.peak_bytes;
    size_t total_allocs = g_memtrack.stats.total_allocs;
    size_t current_bytes = g_memtrack.stats.current_bytes;
    size_t current_count = g_memtrack.stats.current_count;
    int32_t pool_count = g_memtrack.pool_count;
    int32_t arena_count = g_memtrack.arena_count;

    unlock_tracker();

    // Report leaks WITHOUT holding lock to avoid deadlock if logging allocates
    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG && g_memtrack.alloc_map) {
        if (leak_count > 0) {
            memtrack_report_error("memtrack: %zu memory leaks detected!", leak_count);
            memtrack_log_allocations();
        } else {
            log_info("memtrack: no memory leaks detected");
        }
    } else if (g_memtrack.mode == MEMTRACK_MODE_STATS && current_count > 0) {
        memtrack_report_error("memtrack: LEAK — %zu allocations (%zu bytes) still live at shutdown",
                 current_count, current_bytes);
        // log per-category breakdown
        for (int i = 0; i < MEM_CAT_COUNT; i++) {
            MemtrackCategoryStats* cs = &g_memtrack.stats.categories[i];
            if (cs->current_count > 0) {
                memtrack_report_error("memtrack:   %s: %zu allocs, %zu bytes",
                         memtrack_category_names[i], cs->current_count, cs->current_bytes);
            }
        }
        memtrack_report_error("memtrack:   allocation sites:");
        memtrack_log_live_line_stats();
    }

    if (pool_count > 0) {
        memtrack_report_warn("memtrack: %d pool(s) not destroyed at shutdown", pool_count);
    }
    if (arena_count > 0) {
        memtrack_report_warn("memtrack: %d arena(s) not destroyed at shutdown", arena_count);
    }

    // Log final stats WITHOUT holding lock
    if (leak_count == 0 && pool_count == 0 && arena_count == 0) {
        log_info("memtrack: clean shutdown — 0 live allocations");
    }
    log_info("memtrack: shutdown - peak usage: %zu bytes, total allocs: %zu",
             peak_bytes, total_allocs);

    // Now safely clean up with lock
    lock_tracker();

    if (g_memtrack.alloc_map) {
        hashmap_free(g_memtrack.alloc_map);
    }

    unlock_tracker();
    pthread_mutex_destroy(&g_memtrack.lock);

    g_memtrack.initialized = false;

    return leak_count;
}

MemtrackMode memtrack_get_mode(void) {
    return g_memtrack.mode;
}

void memtrack_set_mode(MemtrackMode mode) {
    lock_tracker();

    // Transitioning to debug mode requires creating the allocation map
    if (mode == MEMTRACK_MODE_DEBUG && !g_memtrack.alloc_map) {
        // Use raw malloc/realloc/free to avoid tracking the hashmap itself
        g_memtrack.alloc_map = hashmap_new_with_allocator(
            malloc, realloc, free,  // Use raw allocators
            sizeof(AllocInfo),    // Element size
            MAX_TRACKED_ALLOCS,  // Initial capacity
            0, 0,                // Random seeds
            hash_ptr,            // Hash function
            cmp_ptr,             // Compare function
            NULL,                // No element free function
            NULL                 // No user data
        );
    }

    g_memtrack.mode = mode;
    unlock_tracker();
}

// ============================================================================
// Allocation Implementation
// ============================================================================

void* mem_alloc_loc(size_t size, MemCategory category, int line)
{
    if (!g_memtrack.initialized || g_memtrack.mode == MEMTRACK_MODE_OFF || !tls_tracking_enabled) {
        return malloc(size);
    }

    void* user_ptr = NULL;
    void* real_ptr = NULL;
    size_t real_size = size;

    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        // Allocate with guard bytes
        real_size = GUARD_SIZE + size + GUARD_SIZE;
        real_ptr = malloc(real_size);
        if (!real_ptr) return NULL;

        // Fill guards
        fill_guard_bytes(real_ptr, GUARD_SIZE, GUARD_BYTE_HEAD);
        fill_guard_bytes((char*)real_ptr + GUARD_SIZE + size, GUARD_SIZE, GUARD_BYTE_TAIL);

        // Fill user memory with pattern (helps detect use of uninitialized)
        user_ptr = (char*)real_ptr + GUARD_SIZE;
        memset(user_ptr, FILL_BYTE_ALLOC, size);
    } else {
        // STATS mode: prepend MemAllocHeader so mem_free can recover size/category
        real_ptr = malloc(sizeof(MemAllocHeader) + size);
        if (!real_ptr) return NULL;
        MemAllocHeader* hdr = (MemAllocHeader*)real_ptr;
        hdr->size = (uint32_t)size;
        hdr->category = (uint16_t)category;
        hdr->magic = MEMTRACK_STATS_MAGIC;
        hdr->line = line;
        hdr->reserved = 0;
        user_ptr = (void*)(hdr + 1);
    }

    lock_tracker();

    // Update stats
    update_category_stats_alloc(category, size);
    update_global_stats_alloc(size);
    if (g_memtrack.mode == MEMTRACK_MODE_STATS) {
        update_line_stats_alloc(category, size, line);
    }

    // Record allocation in debug mode
    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        AllocInfo info = {
            .ptr = user_ptr,
            .real_ptr = real_ptr,
            .size = size,
            .real_size = real_size,
            .category = category,
            .alloc_id = g_memtrack.next_alloc_id++
        };
        info.line = line;
        hashmap_set(g_memtrack.alloc_map, &info);
    }

    // Check memory pressure
    MemPressureLevel pressure = compute_pressure_level(g_memtrack.stats.current_bytes);
    if (pressure >= MEM_PRESSURE_LOW) {
        trigger_pressure_callbacks(pressure, size);
    }

    unlock_tracker();

    return user_ptr;
}

void* mem_alloc(size_t size, MemCategory category) {
    return mem_alloc_loc(size, category, 0);
}

void* mem_calloc_loc(size_t count, size_t size, MemCategory category, int line)
{
    size_t total = count * size;
    void* ptr = mem_alloc_loc(total, category, line);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* mem_calloc(size_t count, size_t size, MemCategory category) {
    return mem_calloc_loc(count, size, category, 0);
}

void* mem_realloc_loc(void* ptr, size_t new_size, MemCategory category, int line)
{
    if (!ptr) {
        return mem_alloc_loc(new_size, category, line);
    }

    if (new_size == 0) {
        mem_free_loc(ptr, line);
        return NULL;
    }

    if (!g_memtrack.initialized || g_memtrack.mode == MEMTRACK_MODE_OFF || !tls_tracking_enabled) {
        return realloc(ptr, new_size);
    }

    // In STATS mode, realloc via header-aware path
    if (g_memtrack.mode == MEMTRACK_MODE_STATS) {
        MemAllocHeader* old_hdr = ((MemAllocHeader*)ptr) - 1;
        size_t old_size = 0;
        MemCategory old_cat = category;
        int old_line = 0;
        if (old_hdr->magic == MEMTRACK_STATS_MAGIC) {
            old_size = old_hdr->size;
            old_cat = (MemCategory)old_hdr->category;
            old_line = old_hdr->line;
        }
        // realloc the real block (header + user data)
        MemAllocHeader* new_hdr = (MemAllocHeader*)realloc(old_hdr, sizeof(MemAllocHeader) + new_size);
        if (!new_hdr) return NULL;

        lock_tracker();
        // remove old stats
        if (old_size > 0) {
            update_category_stats_free(old_cat, old_size);
            update_global_stats_free(old_size);
            update_line_stats_free(old_cat, old_size, old_line);
        }
        // add new stats
        update_category_stats_alloc(category, new_size);
        update_global_stats_alloc(new_size);
        update_line_stats_alloc(category, new_size, line);
        unlock_tracker();

        new_hdr->size = (uint32_t)new_size;
        new_hdr->category = (uint16_t)category;
        new_hdr->magic = MEMTRACK_STATS_MAGIC;
        new_hdr->line = line;
        new_hdr->reserved = 0;
        return (void*)(new_hdr + 1);
    }

    // DEBUG mode: need to track old allocation info for proper memcpy
    size_t old_size = 0;

    lock_tracker();
    AllocInfo key = {.ptr = ptr};
    const AllocInfo* info = (const AllocInfo*)hashmap_get(g_memtrack.alloc_map, &key);
    if (info) {
        old_size = info->size;
    }
    unlock_tracker();

    // Allocate new
    void* new_ptr = mem_alloc_loc(new_size, category, line);

    if (new_ptr && old_size > 0) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }

    // Free old
    mem_free_loc(ptr, line);

    return new_ptr;
}

void* mem_realloc(void* ptr, size_t new_size, MemCategory category) {
    return mem_realloc_loc(ptr, new_size, category, 0);
}

void mem_free_loc(void* ptr, int line)
{
    if (!ptr) return;

    if (!g_memtrack.initialized || g_memtrack.mode == MEMTRACK_MODE_OFF || !tls_tracking_enabled) {
        free(ptr);
        return;
    }

    lock_tracker();

    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        AllocInfo key = {.ptr = ptr};
        const AllocInfo* info = (const AllocInfo*)hashmap_get(g_memtrack.alloc_map, &key);

        if (!info) {
            g_memtrack.stats.invalid_frees++;
            if (line > 0) {
                memtrack_report_error("memtrack: invalid free at line %d - pointer %p not tracked", line, ptr);
            } else {
                memtrack_report_error("memtrack: invalid free - pointer %p not tracked", ptr);
            }
            unlock_tracker();
            return;  // Don't free unknown pointer
        }

        // Verify guard bytes
        bool head_ok = verify_guard_bytes(info->real_ptr, GUARD_SIZE, GUARD_BYTE_HEAD);
        bool tail_ok = verify_guard_bytes((char*)info->real_ptr + GUARD_SIZE + info->size,
                                          GUARD_SIZE, GUARD_BYTE_TAIL);

        if (!head_ok || !tail_ok) {
            g_memtrack.stats.guard_violations++;
            if (line > 0 && info->line > 0) {
                memtrack_report_error("memtrack: buffer overflow detected at line %d for allocation %p "
                         "(alloc line %d, size=%zu, category=%s)",
                         line, ptr, info->line, info->size,
                         memtrack_category_names[info->category]);
            } else if (info->line > 0) {
                memtrack_report_error("memtrack: buffer overflow detected for allocation %p "
                         "(alloc line %d, size=%zu, category=%s)",
                         ptr, info->line, info->size,
                         memtrack_category_names[info->category]);
            } else {
                memtrack_report_error("memtrack: buffer overflow detected for allocation %p (size=%zu, category=%s)",
                         ptr, info->size, memtrack_category_names[info->category]);
            }
        }

        // Update stats
        update_category_stats_free(info->category, info->size);
        update_global_stats_free(info->size);

        // Fill freed memory with pattern (helps detect use-after-free)
        memset(info->real_ptr, FILL_BYTE_FREE, info->real_size);

        // Free the actual memory
        free(info->real_ptr);

        // Remove from tracking
        hashmap_delete(g_memtrack.alloc_map, &key);

    } else {
        // STATS mode: recover size and category from inline header
        MemAllocHeader* hdr = ((MemAllocHeader*)ptr) - 1;
        if (hdr->magic == MEMTRACK_STATS_MAGIC) {
            size_t alloc_size = hdr->size;
            MemCategory cat = (MemCategory)hdr->category;
            int alloc_line = hdr->line;
            update_category_stats_free(cat, alloc_size);
            update_global_stats_free(alloc_size);
            update_line_stats_free(cat, alloc_size, alloc_line);
            hdr->magic = 0;  // invalidate to catch double-free
            free(hdr);
        } else {
            // not a tracked allocation (or double-free) — best-effort
            g_memtrack.stats.current_count--;
            g_memtrack.stats.total_frees++;
            g_memtrack.stats.invalid_frees++;
            memtrack_report_error("memtrack: stats-mode free of untracked pointer %p (bad magic 0x%04X)", ptr, hdr->magic);
            // cannot safely free — we don't know the real base pointer
        }
    }

    unlock_tracker();
}

void mem_free(void* ptr) {
    mem_free_loc(ptr, 0);
}

char* mem_strdup_loc(const char* str, MemCategory category, int line) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)mem_alloc_loc(len, category, line);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

char* mem_strdup(const char* str, MemCategory category) {
    return mem_strdup_loc(str, category, 0);
}

char* mem_strndup_loc(const char* str, size_t max_len, MemCategory category, int line) {
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len > max_len) len = max_len;
    char* dup = (char*)mem_alloc_loc(len + 1, category, line);
    if (dup) {
        memcpy(dup, str, len);
        dup[len] = '\0';
    }
    return dup;
}

char* mem_strndup(const char* str, size_t max_len, MemCategory category) {
    return mem_strndup_loc(str, max_len, category, 0);
}

// ============================================================================
// Query API Implementation
// ============================================================================

void memtrack_get_stats(MemtrackStats* stats) {
    lock_tracker();
    memcpy(stats, &g_memtrack.stats, sizeof(MemtrackStats));
    if (stats->current_count == 0) {
        // live-byte telemetry must follow the live-allocation invariant; the
        // debug allocation map is authoritative when every allocation is gone.
        stats->current_bytes = 0;
    }
    unlock_tracker();
}

void memtrack_get_category_stats(MemCategory category, MemtrackCategoryStats* stats) {
    lock_tracker();
    memcpy(stats, &g_memtrack.stats.categories[category], sizeof(MemtrackCategoryStats));
    if (stats->current_count == 0) {
        // keep per-category live bytes consistent with no live allocations.
        stats->current_bytes = 0;
    }
    unlock_tracker();
}

bool memtrack_get_alloc_info(void* ptr, size_t* out_size, MemCategory* out_category) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return false;

    lock_tracker();
    AllocInfo key = {.ptr = ptr};
    const AllocInfo* info = (const AllocInfo*)hashmap_get(g_memtrack.alloc_map, &key);
    if (info) {
        if (out_size) *out_size = info->size;
        if (out_category) *out_category = info->category;
        unlock_tracker();
        return true;
    }
    unlock_tracker();
    return false;
}

bool memtrack_is_allocated(void* ptr) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return false;

    lock_tracker();
    AllocInfo key = {.ptr = ptr};
    bool found = hashmap_get(g_memtrack.alloc_map, &key) != NULL;
    unlock_tracker();
    return found;
}

size_t memtrack_get_current_usage(void) {
    return g_memtrack.stats.current_bytes;
}

size_t memtrack_get_peak_usage(void) {
    return g_memtrack.stats.peak_bytes;
}

size_t memtrack_get_category_usage(MemCategory category) {
    return g_memtrack.stats.categories[category].current_bytes;
}

// ============================================================================
// Memory Pressure API Implementation
// ============================================================================

uint32_t memtrack_register_pressure_callback(
    MemPressureCallback callback,
    void* user_data,
    uint64_t categories
) {
    lock_tracker();

    for (int i = 0; i < MAX_PRESSURE_CALLBACKS; i++) {
        if (!g_memtrack.pressure_callbacks[i].active) {
            PressureCallbackEntry* entry = &g_memtrack.pressure_callbacks[i];
            entry->callback = callback;
            entry->user_data = user_data;
            entry->categories = categories;
            entry->handle = g_memtrack.next_callback_handle++;
            entry->active = true;

            unlock_tracker();
            return entry->handle;
        }
    }

    unlock_tracker();
    memtrack_report_error("memtrack: max pressure callbacks reached");
    return 0;
}

void memtrack_unregister_pressure_callback(uint32_t handle) {
    lock_tracker();

    for (int i = 0; i < MAX_PRESSURE_CALLBACKS; i++) {
        if (g_memtrack.pressure_callbacks[i].active &&
            g_memtrack.pressure_callbacks[i].handle == handle) {
            g_memtrack.pressure_callbacks[i].active = false;
            break;
        }
    }

    unlock_tracker();
}

void memtrack_set_limits(size_t soft_limit, size_t hard_limit, size_t critical_limit) {
    lock_tracker();
    g_memtrack.soft_limit = soft_limit;
    g_memtrack.hard_limit = hard_limit;
    g_memtrack.critical_limit = critical_limit;
    unlock_tracker();
}

MemPressureLevel memtrack_get_pressure_level(void) {
    return compute_pressure_level(g_memtrack.stats.current_bytes);
}

size_t memtrack_trigger_pressure(MemPressureLevel level) {
    size_t freed_before = g_memtrack.stats.current_bytes;

    lock_tracker();
    trigger_pressure_callbacks(level, 0);
    unlock_tracker();

    return freed_before - g_memtrack.stats.current_bytes;
}

size_t memtrack_request_free(size_t bytes_needed) {
    size_t freed_before = g_memtrack.stats.current_bytes;

    lock_tracker();
    trigger_pressure_callbacks(MEM_PRESSURE_MEDIUM, bytes_needed);
    unlock_tracker();

    size_t freed = freed_before - g_memtrack.stats.current_bytes;

    // If not enough freed, escalate
    if (freed < bytes_needed) {
        lock_tracker();
        trigger_pressure_callbacks(MEM_PRESSURE_HIGH, bytes_needed - freed);
        unlock_tracker();
        freed = freed_before - g_memtrack.stats.current_bytes;
    }

    return freed;
}

// ============================================================================
// Debug / Profiling Implementation
// ============================================================================

void memtrack_log_usage(void) {
    lock_tracker();

    log_info("memtrack: === Memory Usage Report ===");
    log_info("memtrack: Current: %zu bytes (%zu allocs)",
             g_memtrack.stats.current_bytes, g_memtrack.stats.current_count);
    log_info("memtrack: Peak: %zu bytes (%zu allocs)",
             g_memtrack.stats.peak_bytes, g_memtrack.stats.peak_count);
    log_info("memtrack: Total: %zu allocs, %zu frees",
             g_memtrack.stats.total_allocs, g_memtrack.stats.total_frees);

    log_info("memtrack: --- By Category ---");
    for (int i = 0; i < MEM_CAT_COUNT; i++) {
        MemtrackCategoryStats* cs = &g_memtrack.stats.categories[i];
        if (cs->current_bytes > 0 || cs->total_allocs > 0) {
            log_info("memtrack: %-15s: %10zu bytes (%zu allocs), peak: %zu",
                    memtrack_category_names[i], cs->current_bytes,
                    cs->current_count, cs->peak_bytes);
        }
    }

    unlock_tracker();
}

// Callback for iterating allocations
typedef struct LogAllocCtx {
    int count;
    int max_show;
} LogAllocCtx;

static bool log_alloc_iter(const void* item, void* udata) {
    LogAllocCtx* ctx = (LogAllocCtx*)udata;
    const AllocInfo* info = (const AllocInfo*)item;

    if (ctx->count < ctx->max_show) {
        if (info->line > 0) {
            memtrack_report_error("memtrack: leak #%d: %p, %zu bytes, category=%s, alloc line %d",
                    ctx->count + 1, info->ptr, info->size,
                    memtrack_category_names[info->category],
                    info->line);
        } else {
            memtrack_report_error("memtrack: leak #%d: %p, %zu bytes, category=%s",
                    ctx->count + 1, info->ptr, info->size,
                    memtrack_category_names[info->category]);
        }
    }
    ctx->count++;
    return true;  // Continue iteration
}

void memtrack_log_allocations(void) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) {
        memtrack_report_warn("memtrack: detailed allocation logging requires DEBUG mode");
        return;
    }

    lock_tracker();

    size_t count = hashmap_count(g_memtrack.alloc_map);
    log_info("memtrack: === Active Allocations (%zu) ===", count);

    LogAllocCtx ctx = {0, 100};  // Show first 100
    hashmap_scan(g_memtrack.alloc_map, log_alloc_iter, &ctx);

    if (ctx.count > ctx.max_show) {
        log_info("memtrack: ... and %d more", ctx.count - ctx.max_show);
    }

    unlock_tracker();
}

void memtrack_log_category(MemCategory category) {
    // Would require storing per-category allocation lists
    // For now, just log category stats
    MemtrackCategoryStats stats;
    memtrack_get_category_stats(category, &stats);

    log_info("memtrack: Category '%s': %zu bytes, %zu allocs (peak: %zu bytes)",
            memtrack_category_names[category],
            stats.current_bytes, stats.current_count, stats.peak_bytes);
}

size_t memtrack_check_leaks(void) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return 0;

    lock_tracker();
    size_t leaks = hashmap_count(g_memtrack.alloc_map);
    unlock_tracker();

    return leaks;
}

static bool verify_guards_iter(const void* item, void* udata) {
    const AllocInfo* info = (const AllocInfo*)item;
    size_t* violations = (size_t*)udata;

    bool head_ok = verify_guard_bytes(info->real_ptr, GUARD_SIZE, GUARD_BYTE_HEAD);
    bool tail_ok = verify_guard_bytes((char*)info->real_ptr + GUARD_SIZE + info->size,
                                      GUARD_SIZE, GUARD_BYTE_TAIL);

    if (!head_ok || !tail_ok) {
        (*violations)++;
        if (info->line > 0) {
            memtrack_report_error("memtrack: guard violation at %p (alloc line %d)",
                     info->ptr, info->line);
        } else {
            memtrack_report_error("memtrack: guard violation at %p", info->ptr);
        }
    }

    return true;
}

size_t memtrack_verify_guards(void) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return 0;

    lock_tracker();
    size_t violations = 0;
    hashmap_scan(g_memtrack.alloc_map, verify_guards_iter, &violations);
    unlock_tracker();

    return violations;
}

uint32_t memtrack_snapshot(void) {
    lock_tracker();

    for (int i = 0; i < MAX_SNAPSHOTS; i++) {
        if (!g_memtrack.snapshots[i].active) {
            SnapshotEntry* snap = &g_memtrack.snapshots[i];
            snap->handle = g_memtrack.next_snapshot_handle++;
            snap->active = true;
            memcpy(&snap->stats, &g_memtrack.stats, sizeof(MemtrackStats));

            unlock_tracker();
            return snap->handle;
        }
    }

    unlock_tracker();
    memtrack_report_error("memtrack: max snapshots reached");
    return 0;
}

void memtrack_compare_snapshot(uint32_t snapshot_handle) {
    lock_tracker();

    SnapshotEntry* snap = NULL;
    for (int i = 0; i < MAX_SNAPSHOTS; i++) {
        if (g_memtrack.snapshots[i].active &&
            g_memtrack.snapshots[i].handle == snapshot_handle) {
            snap = &g_memtrack.snapshots[i];
            break;
        }
    }

    if (!snap) {
        unlock_tracker();
        memtrack_report_error("memtrack: snapshot %u not found", snapshot_handle);
        return;
    }

    int64_t bytes_diff = (int64_t)g_memtrack.stats.current_bytes - (int64_t)snap->stats.current_bytes;
    int64_t count_diff = (int64_t)g_memtrack.stats.current_count - (int64_t)snap->stats.current_count;

    log_info("memtrack: === Snapshot Comparison ===");
    log_info("memtrack: Bytes: %+lld (%zu -> %zu)",
             bytes_diff, snap->stats.current_bytes, g_memtrack.stats.current_bytes);
    log_info("memtrack: Allocs: %+lld (%zu -> %zu)",
             count_diff, snap->stats.current_count, g_memtrack.stats.current_count);

    log_info("memtrack: --- Category Changes ---");
    for (int i = 0; i < MEM_CAT_COUNT; i++) {
        int64_t cat_diff = (int64_t)g_memtrack.stats.categories[i].current_bytes -
                          (int64_t)snap->stats.categories[i].current_bytes;
        if (cat_diff != 0) {
            log_info("memtrack: %-15s: %+lld bytes", memtrack_category_names[i], cat_diff);
        }
    }

    unlock_tracker();
}

void memtrack_free_snapshot(uint32_t snapshot_handle) {
    lock_tracker();

    for (int i = 0; i < MAX_SNAPSHOTS; i++) {
        if (g_memtrack.snapshots[i].active &&
            g_memtrack.snapshots[i].handle == snapshot_handle) {
            g_memtrack.snapshots[i].active = false;
            break;
        }
    }

    unlock_tracker();
}

void memtrack_thread_enable(bool enable) {
    tls_tracking_enabled = enable;
}

// ============================================================================
// Pool/Arena Integration
// ============================================================================

// These would integrate with your existing Pool/Arena but add tracking
// Implementation depends on your Pool/Arena internals

Pool* memtrack_pool_create(MemCategory category) {
    (void)category;
    extern Pool* pool_create(void);
    Pool* pool = pool_create();
    if (pool) {
        lock_tracker();
        g_memtrack.pool_count++;
        unlock_tracker();
    }
    return pool;
}

void* memtrack_pool_alloc(Pool* pool, size_t size) {
    // pool allocations are bulk-freed; no per-object tracking
    extern void* pool_alloc(Pool* pool, size_t size);
    return pool_alloc(pool, size);
}

void memtrack_pool_destroy(Pool* pool) {
    extern void pool_destroy(Pool* pool);
    if (pool) {
        lock_tracker();
        g_memtrack.pool_count--;
        unlock_tracker();
    }
    pool_destroy(pool);
}

Arena* memtrack_arena_create(Pool* pool, MemCategory category) {
    (void)category;
    extern Arena* arena_create_default(Pool* pool);
    Arena* arena = arena_create_default(pool);
    if (arena) {
        lock_tracker();
        g_memtrack.arena_count++;
        unlock_tracker();
    }
    return arena;
}

void* memtrack_arena_alloc(Arena* arena, size_t size) {
    // arena allocations are bulk-freed; no per-object tracking
    extern void* arena_alloc(Arena* arena, size_t size);
    return arena_alloc(arena, size);
}

void memtrack_arena_destroy(Arena* arena) {
    extern void arena_destroy(Arena* arena);
    if (arena) {
        lock_tracker();
        g_memtrack.arena_count--;
        unlock_tracker();
    }
    arena_destroy(arena);
}

int32_t memtrack_get_pool_count(void) {
    return g_memtrack.pool_count;
}

int32_t memtrack_get_arena_count(void) {
    return g_memtrack.arena_count;
}

// ============================================================================
// Raw Allocation Escape Hatches
// ============================================================================

void* raw_malloc(size_t size) {
    return malloc(size);
}

void* raw_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

void* raw_realloc(void* ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

void raw_free(void* ptr) {
    free(ptr);
}

char* raw_strdup(const char* str) {
    return str ? strdup(str) : NULL;
}
