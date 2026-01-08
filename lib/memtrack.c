/**
 * Lambda Memory Tracker Implementation
 *
 * A lightweight memory tracking system with:
 * - Category-based allocation tracking
 * - Debug mode with guard bytes and leak detection
 * - Memory pressure callbacks for runtime management
 */

#include "memtrack.h"
#include "hashmap.h"
#include "arraylist.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

// Guard byte patterns for buffer overflow detection
#define GUARD_BYTE_HEAD     0xDE
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

// ============================================================================
// Category Names
// ============================================================================

const char* memtrack_category_names[LMEM_CAT_COUNT] = {
    [LMEM_CAT_UNKNOWN]      = "unknown",
    [LMEM_CAT_AST]          = "ast",
    [LMEM_CAT_PARSER]       = "parser",
    [LMEM_CAT_EVAL]         = "eval",
    [LMEM_CAT_STRING]       = "string",
    [LMEM_CAT_CONTAINER]    = "container",
    [LMEM_CAT_NAMEPOOL]     = "namepool",
    [LMEM_CAT_SHAPEPOOL]    = "shapepool",
    [LMEM_CAT_INPUT_JSON]   = "input-json",
    [LMEM_CAT_INPUT_XML]    = "input-xml",
    [LMEM_CAT_INPUT_HTML]   = "input-html",
    [LMEM_CAT_INPUT_CSS]    = "input-css",
    [LMEM_CAT_INPUT_MD]     = "input-md",
    [LMEM_CAT_INPUT_PDF]    = "input-pdf",
    [LMEM_CAT_INPUT_OTHER]  = "input-other",
    [LMEM_CAT_FORMAT]       = "format",
    [LMEM_CAT_DOM]          = "dom",
    [LMEM_CAT_LAYOUT]       = "layout",
    [LMEM_CAT_STYLE]        = "style",
    [LMEM_CAT_FONT]         = "font",
    [LMEM_CAT_IMAGE]        = "image",
    [LMEM_CAT_RENDER]       = "render",
    [LMEM_CAT_CACHE_FONT]   = "cache-font",
    [LMEM_CAT_CACHE_IMAGE]  = "cache-image",
    [LMEM_CAT_CACHE_LAYOUT] = "cache-layout",
    [LMEM_CAT_CACHE_OTHER]  = "cache-other",
    [LMEM_CAT_TEMP]         = "temp",
};

// ============================================================================
// Internal Structures
// ============================================================================

// Allocation record (debug mode only)
typedef struct AllocRecord {
    void* user_ptr;             // Pointer returned to user
    void* real_ptr;             // Actual allocation (with guards)
    size_t size;                // User-requested size
    size_t real_size;           // Actual allocated size (with guards)
    MemCategory category;
    uint64_t alloc_id;          // Monotonic allocation ID
#ifdef MEMTRACK_DEBUG_LOCATIONS
    const char* file;
    int line;
#endif
} AllocRecord;

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

// Global tracker state
typedef struct MemtrackState {
    MemtrackMode mode;
    bool initialized;

    // Statistics (always maintained)
    MemtrackStats stats;

    // Lock for thread safety
    pthread_mutex_t lock;

    // Allocation registry (debug mode only)
    HashMap* alloc_map;         // ptr -> AllocRecord*
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

} MemtrackState;

// Global state
static MemtrackState g_memtrack = {0};

// Thread-local tracking enable flag
static __thread bool tls_tracking_enabled = true;

// ============================================================================
// Internal Helpers
// ============================================================================

static inline void lock_tracker(void) {
    pthread_mutex_lock(&g_memtrack.lock);
}

static inline void unlock_tracker(void) {
    pthread_mutex_unlock(&g_memtrack.lock);
}

static uint64_t hash_ptr(const void* key) {
    return (uint64_t)key * 0x9E3779B97F4A7C15ULL;
}

static int cmp_ptr(const void* a, const void* b) {
    return (a == b) ? 0 : ((a < b) ? -1 : 1);
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
        log_warn("memtrack: already initialized");
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
        g_memtrack.alloc_map = hashmap_new(hash_ptr, cmp_ptr, MAX_TRACKED_ALLOCS);
        if (!g_memtrack.alloc_map) {
            log_error("memtrack: failed to create allocation map");
            return false;
        }
    }

    g_memtrack.initialized = true;
    log_info("memtrack: initialized in %s mode",
             mode == MEMTRACK_MODE_OFF ? "OFF" :
             mode == MEMTRACK_MODE_STATS ? "STATS" : "DEBUG");

    return true;
}

void memtrack_shutdown(void) {
    if (!g_memtrack.initialized) {
        return;
    }

    lock_tracker();

    // Report leaks in debug mode
    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG && g_memtrack.alloc_map) {
        size_t leak_count = hashmap_size(g_memtrack.alloc_map);
        if (leak_count > 0) {
            log_warn("memtrack: %zu memory leaks detected!", leak_count);
            memtrack_log_allocations();
        } else {
            log_info("memtrack: no memory leaks detected");
        }
        hashmap_free(g_memtrack.alloc_map);
    }

    // Log final stats
    log_info("memtrack: shutdown - peak usage: %zu bytes, total allocs: %zu",
             g_memtrack.stats.peak_bytes, g_memtrack.stats.total_allocs);

    unlock_tracker();
    pthread_mutex_destroy(&g_memtrack.lock);

    g_memtrack.initialized = false;
}

MemtrackMode memtrack_get_mode(void) {
    return g_memtrack.mode;
}

void memtrack_set_mode(MemtrackMode mode) {
    lock_tracker();

    // Transitioning to debug mode requires creating the allocation map
    if (mode == MEMTRACK_MODE_DEBUG && !g_memtrack.alloc_map) {
        g_memtrack.alloc_map = hashmap_new(hash_ptr, cmp_ptr, MAX_TRACKED_ALLOCS);
    }

    g_memtrack.mode = mode;
    unlock_tracker();
}

// ============================================================================
// Allocation Implementation
// ============================================================================

#ifdef MEMTRACK_DEBUG_LOCATIONS
void* lmem_alloc_loc(size_t size, MemCategory category, const char* file, int line)
#else
void* lmem_alloc(size_t size, MemCategory category)
#endif
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
        user_ptr = malloc(size);
        real_ptr = user_ptr;
        if (!user_ptr) return NULL;
    }

    lock_tracker();

    // Update stats
    update_category_stats_alloc(category, size);
    update_global_stats_alloc(size);

    // Record allocation in debug mode
    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        AllocRecord* record = (AllocRecord*)malloc(sizeof(AllocRecord));
        record->user_ptr = user_ptr;
        record->real_ptr = real_ptr;
        record->size = size;
        record->real_size = real_size;
        record->category = category;
        record->alloc_id = g_memtrack.next_alloc_id++;
#ifdef MEMTRACK_DEBUG_LOCATIONS
        record->file = file;
        record->line = line;
#endif
        hashmap_put(g_memtrack.alloc_map, user_ptr, record);
    }

    // Check memory pressure
    MemPressureLevel pressure = compute_pressure_level(g_memtrack.stats.current_bytes);
    if (pressure >= MEM_PRESSURE_LOW) {
        trigger_pressure_callbacks(pressure, size);
    }

    unlock_tracker();

    return user_ptr;
}

#ifdef MEMTRACK_DEBUG_LOCATIONS
void* lmem_calloc_loc(size_t count, size_t size, MemCategory category, const char* file, int line)
#else
void* lmem_calloc(size_t count, size_t size, MemCategory category)
#endif
{
    size_t total = count * size;
#ifdef MEMTRACK_DEBUG_LOCATIONS
    void* ptr = lmem_alloc_loc(total, category, file, line);
#else
    void* ptr = lmem_alloc(total, category);
#endif
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

#ifdef MEMTRACK_DEBUG_LOCATIONS
void* lmem_realloc_loc(void* ptr, size_t new_size, MemCategory category, const char* file, int line)
#else
void* lmem_realloc(void* ptr, size_t new_size, MemCategory category)
#endif
{
    if (!ptr) {
#ifdef MEMTRACK_DEBUG_LOCATIONS
        return lmem_alloc_loc(new_size, category, file, line);
#else
        return lmem_alloc(new_size, category);
#endif
    }

    if (new_size == 0) {
#ifdef MEMTRACK_DEBUG_LOCATIONS
        lmem_free_loc(ptr, file, line);
#else
        lmem_free(ptr);
#endif
        return NULL;
    }

    if (!g_memtrack.initialized || g_memtrack.mode == MEMTRACK_MODE_OFF || !tls_tracking_enabled) {
        return realloc(ptr, new_size);
    }

    // Get old allocation info
    size_t old_size = 0;
    MemCategory old_category = category;

    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        lock_tracker();
        AllocRecord* record = (AllocRecord*)hashmap_get(g_memtrack.alloc_map, ptr);
        if (record) {
            old_size = record->size;
            old_category = record->category;
        }
        unlock_tracker();
    }

    // Allocate new
#ifdef MEMTRACK_DEBUG_LOCATIONS
    void* new_ptr = lmem_alloc_loc(new_size, category, file, line);
#else
    void* new_ptr = lmem_alloc(new_size, category);
#endif

    if (new_ptr && old_size > 0) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }

    // Free old
#ifdef MEMTRACK_DEBUG_LOCATIONS
    lmem_free_loc(ptr, file, line);
#else
    lmem_free(ptr);
#endif

    return new_ptr;
}

#ifdef MEMTRACK_DEBUG_LOCATIONS
void lmem_free_loc(void* ptr, const char* file, int line)
#else
void lmem_free(void* ptr)
#endif
{
    if (!ptr) return;

    if (!g_memtrack.initialized || g_memtrack.mode == MEMTRACK_MODE_OFF || !tls_tracking_enabled) {
        free(ptr);
        return;
    }

    lock_tracker();

    if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
        AllocRecord* record = (AllocRecord*)hashmap_get(g_memtrack.alloc_map, ptr);

        if (!record) {
            g_memtrack.stats.invalid_frees++;
#ifdef MEMTRACK_DEBUG_LOCATIONS
            log_error("memtrack: invalid free at %s:%d - pointer %p not tracked", file, line, ptr);
#else
            log_error("memtrack: invalid free - pointer %p not tracked", ptr);
#endif
            unlock_tracker();
            return;  // Don't free unknown pointer
        }

        // Verify guard bytes
        bool head_ok = verify_guard_bytes(record->real_ptr, GUARD_SIZE, GUARD_BYTE_HEAD);
        bool tail_ok = verify_guard_bytes((char*)record->real_ptr + GUARD_SIZE + record->size,
                                          GUARD_SIZE, GUARD_BYTE_TAIL);

        if (!head_ok || !tail_ok) {
            g_memtrack.stats.guard_violations++;
#ifdef MEMTRACK_DEBUG_LOCATIONS
            log_error("memtrack: buffer overflow detected at %s:%d for allocation %p "
                     "(alloc'd at %s:%d, size=%zu, category=%s)",
                     file, line, ptr, record->file, record->line, record->size,
                     memtrack_category_names[record->category]);
#else
            log_error("memtrack: buffer overflow detected for allocation %p (size=%zu, category=%s)",
                     ptr, record->size, memtrack_category_names[record->category]);
#endif
        }

        // Update stats
        update_category_stats_free(record->category, record->size);
        update_global_stats_free(record->size);

        // Fill freed memory with pattern (helps detect use-after-free)
        memset(record->real_ptr, FILL_BYTE_FREE, record->real_size);

        // Free the actual memory
        free(record->real_ptr);

        // Remove from tracking
        hashmap_remove(g_memtrack.alloc_map, ptr);
        free(record);

    } else {
        // Stats-only mode: we don't know the size, so we can't update accurately
        // In production, you might want to store size in a header
        g_memtrack.stats.current_count--;
        g_memtrack.stats.total_frees++;
        free(ptr);
    }

    unlock_tracker();
}

char* lmem_strdup(const char* str, MemCategory category) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)lmem_alloc(len, category);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

char* lmem_strndup(const char* str, size_t max_len, MemCategory category) {
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len > max_len) len = max_len;
    char* dup = (char*)lmem_alloc(len + 1, category);
    if (dup) {
        memcpy(dup, str, len);
        dup[len] = '\0';
    }
    return dup;
}

// ============================================================================
// Query API Implementation
// ============================================================================

void memtrack_get_stats(MemtrackStats* stats) {
    lock_tracker();
    memcpy(stats, &g_memtrack.stats, sizeof(MemtrackStats));
    unlock_tracker();
}

void memtrack_get_category_stats(MemCategory category, MemtrackCategoryStats* stats) {
    lock_tracker();
    memcpy(stats, &g_memtrack.stats.categories[category], sizeof(MemtrackCategoryStats));
    unlock_tracker();
}

bool memtrack_get_alloc_info(void* ptr, size_t* out_size, MemCategory* out_category) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return false;

    lock_tracker();
    AllocRecord* record = (AllocRecord*)hashmap_get(g_memtrack.alloc_map, ptr);
    if (record) {
        if (out_size) *out_size = record->size;
        if (out_category) *out_category = record->category;
        unlock_tracker();
        return true;
    }
    unlock_tracker();
    return false;
}

bool memtrack_is_allocated(void* ptr) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return false;

    lock_tracker();
    bool found = hashmap_get(g_memtrack.alloc_map, ptr) != NULL;
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
    log_error("memtrack: max pressure callbacks reached");
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
    for (int i = 0; i < LMEM_CAT_COUNT; i++) {
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

static bool log_alloc_iter(const void* key, void* value, void* ctx) {
    LogAllocCtx* log_ctx = (LogAllocCtx*)ctx;
    AllocRecord* record = (AllocRecord*)value;

    if (log_ctx->count < log_ctx->max_show) {
#ifdef MEMTRACK_DEBUG_LOCATIONS
        log_warn("memtrack: leak #%d: %p, %zu bytes, category=%s, alloc'd at %s:%d",
                log_ctx->count + 1, record->user_ptr, record->size,
                memtrack_category_names[record->category],
                record->file, record->line);
#else
        log_warn("memtrack: leak #%d: %p, %zu bytes, category=%s",
                log_ctx->count + 1, record->user_ptr, record->size,
                memtrack_category_names[record->category]);
#endif
    }
    log_ctx->count++;
    return true;  // Continue iteration
}

void memtrack_log_allocations(void) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) {
        log_warn("memtrack: detailed allocation logging requires DEBUG mode");
        return;
    }

    lock_tracker();

    size_t count = hashmap_size(g_memtrack.alloc_map);
    log_info("memtrack: === Active Allocations (%zu) ===", count);

    LogAllocCtx ctx = {0, 100};  // Show first 100
    hashmap_iterate(g_memtrack.alloc_map, log_alloc_iter, &ctx);

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
    size_t leaks = hashmap_size(g_memtrack.alloc_map);
    unlock_tracker();

    return leaks;
}

static bool verify_guards_iter(const void* key, void* value, void* ctx) {
    AllocRecord* record = (AllocRecord*)value;
    size_t* violations = (size_t*)ctx;

    bool head_ok = verify_guard_bytes(record->real_ptr, GUARD_SIZE, GUARD_BYTE_HEAD);
    bool tail_ok = verify_guard_bytes((char*)record->real_ptr + GUARD_SIZE + record->size,
                                      GUARD_SIZE, GUARD_BYTE_TAIL);

    if (!head_ok || !tail_ok) {
        (*violations)++;
#ifdef MEMTRACK_DEBUG_LOCATIONS
        log_error("memtrack: guard violation at %p (alloc'd at %s:%d)",
                 record->user_ptr, record->file, record->line);
#else
        log_error("memtrack: guard violation at %p", record->user_ptr);
#endif
    }

    return true;
}

size_t memtrack_verify_guards(void) {
    if (g_memtrack.mode != MEMTRACK_MODE_DEBUG) return 0;

    lock_tracker();
    size_t violations = 0;
    hashmap_iterate(g_memtrack.alloc_map, verify_guards_iter, &violations);
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
    log_error("memtrack: max snapshots reached");
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
        log_error("memtrack: snapshot %u not found", snapshot_handle);
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
    for (int i = 0; i < LMEM_CAT_COUNT; i++) {
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
    // TODO: Create pool and associate category
    // For now, just create regular pool
    extern Pool* pool_create(void);
    return pool_create();
}

void* memtrack_pool_alloc(Pool* pool, size_t size) {
    // TODO: Track pool allocations
    extern void* pool_alloc(Pool* pool, size_t size);
    return pool_alloc(pool, size);
}

void memtrack_pool_destroy(Pool* pool) {
    // TODO: Update stats when pool is destroyed
    extern void pool_destroy(Pool* pool);
    pool_destroy(pool);
}

Arena* memtrack_arena_create(Pool* pool, MemCategory category) {
    // TODO: Create arena and associate category
    extern Arena* arena_create_default(Pool* pool);
    return arena_create_default(pool);
}

void* memtrack_arena_alloc(Arena* arena, size_t size) {
    // TODO: Track arena allocations
    extern void* arena_alloc(Arena* arena, size_t size);
    return arena_alloc(arena, size);
}

void memtrack_arena_destroy(Arena* arena) {
    // TODO: Update stats when arena is destroyed
    extern void arena_destroy(Arena* arena);
    arena_destroy(arena);
}
