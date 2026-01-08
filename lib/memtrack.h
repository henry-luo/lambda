#ifndef MEMTRACK_H
#define MEMTRACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * Lambda Memory Tracker
 *
 * A lightweight memory tracking system for development and runtime memory management.
 *
 * Features:
 * - Category-based allocation tracking
 * - Type metadata (RTTI) for runtime inspection
 * - Memory leak detection (debug mode)
 * - Buffer overflow detection via guard bytes (debug mode)
 * - Memory usage statistics and profiling
 * - Memory pressure callbacks for cache eviction
 *
 * Usage:
 *   // Initialize at startup
 *   memtrack_init(MEMTRACK_MODE_DEBUG);
 *
 *   // Allocate with category
 *   void* ptr = mem_alloc(1024, MEM_CAT_PARSER);
 *
 *   // Free
 *   mem_free(ptr);
 *
 *   // Query stats
 *   MemtrackStats stats;
 *   memtrack_get_stats(&stats);
 *
 *   // Shutdown (reports leaks in debug mode)
 *   memtrack_shutdown();
 */

// ============================================================================
// Memory Categories - Add new categories as needed
// ============================================================================

typedef enum MemCategory {
    MEM_CAT_UNKNOWN = 0,

    // Lambda core
    MEM_CAT_AST,           // AST nodes
    MEM_CAT_PARSER,        // Parser temporaries
    MEM_CAT_EVAL,          // Evaluation stack/context
    MEM_CAT_STRING,        // String data (non-pooled)
    MEM_CAT_CONTAINER,     // List, Map, Element, Array
    MEM_CAT_NAMEPOOL,      // Name pool entries
    MEM_CAT_SHAPEPOOL,     // Shape pool entries

    // Input parsers
    MEM_CAT_INPUT_JSON,
    MEM_CAT_INPUT_XML,
    MEM_CAT_INPUT_HTML,
    MEM_CAT_INPUT_CSS,
    MEM_CAT_INPUT_MD,
    MEM_CAT_INPUT_PDF,
    MEM_CAT_INPUT_INI,
    MEM_CAT_INPUT_OTHER,

    // Formatters
    MEM_CAT_FORMAT,

    // Radiant layout/render
    MEM_CAT_DOM,           // DOM nodes
    MEM_CAT_LAYOUT,        // Layout computation
    MEM_CAT_STYLE,         // CSS style data
    MEM_CAT_FONT,          // Font data/cache
    MEM_CAT_IMAGE,         // Image data/cache
    MEM_CAT_RENDER,        // Render buffers

    // Caches (evictable under memory pressure)
    MEM_CAT_CACHE_FONT,    // Font glyph cache
    MEM_CAT_CACHE_IMAGE,   // Decoded image cache
    MEM_CAT_CACHE_LAYOUT,  // Layout cache
    MEM_CAT_CACHE_OTHER,

    // Temporary allocations
    MEM_CAT_TEMP,          // Short-lived temporaries

    MEM_CAT_COUNT          // Must be last
} MemCategory;

// Category names for logging/profiling
extern const char* memtrack_category_names[MEM_CAT_COUNT];

// ============================================================================
// Tracker Modes
// ============================================================================

typedef enum MemtrackMode {
    MEMTRACK_MODE_OFF = 0,      // No tracking (zero overhead)
    MEMTRACK_MODE_STATS,        // Stats only (minimal overhead)
    MEMTRACK_MODE_DEBUG,        // Full tracking + guards + leak detection
} MemtrackMode;

// ============================================================================
// Statistics
// ============================================================================

typedef struct MemtrackCategoryStats {
    size_t current_bytes;       // Currently allocated
    size_t current_count;       // Current allocation count
    size_t peak_bytes;          // Peak usage
    size_t peak_count;          // Peak allocation count
    size_t total_allocs;        // Total allocations since init
    size_t total_frees;         // Total frees since init
    size_t total_bytes_alloc;   // Total bytes allocated
} MemtrackCategoryStats;

typedef struct MemtrackStats {
    // Global stats
    size_t current_bytes;
    size_t current_count;
    size_t peak_bytes;
    size_t peak_count;
    size_t total_allocs;
    size_t total_frees;

    // Per-category stats
    MemtrackCategoryStats categories[MEM_CAT_COUNT];

    // Debug mode stats
    size_t guard_violations;    // Buffer overflow detections
    size_t double_frees;        // Double free detections
    size_t invalid_frees;       // Free of untracked pointer
} MemtrackStats;

// ============================================================================
// Memory Pressure Management
// ============================================================================

typedef enum MemPressureLevel {
    MEM_PRESSURE_NONE = 0,      // Normal operation
    MEM_PRESSURE_LOW,           // Start considering eviction
    MEM_PRESSURE_MEDIUM,        // Evict non-essential caches
    MEM_PRESSURE_HIGH,          // Aggressive eviction
    MEM_PRESSURE_CRITICAL,      // Emergency - evict everything possible
} MemPressureLevel;

/**
 * Memory pressure callback
 * @param level Current pressure level
 * @param target_bytes Suggested bytes to free (0 = free as much as possible)
 * @param user_data User context
 * @return Bytes actually freed
 */
typedef size_t (*MemPressureCallback)(MemPressureLevel level, size_t target_bytes, void* user_data);

// ============================================================================
// Initialization / Shutdown
// ============================================================================

/**
 * Initialize the memory tracker
 * @param mode Tracking mode
 * @return true on success
 */
bool memtrack_init(MemtrackMode mode);

/**
 * Shutdown the tracker, reports leaks if in debug mode
 */
void memtrack_shutdown(void);

/**
 * Get current tracking mode
 */
MemtrackMode memtrack_get_mode(void);

/**
 * Set tracking mode at runtime (for toggling debug features)
 */
void memtrack_set_mode(MemtrackMode mode);

// ============================================================================
// Allocation API
// ============================================================================

/**
 * Allocate tracked memory
 * @param size Bytes to allocate
 * @param category Memory category for tracking
 * @return Pointer to allocated memory, or NULL on failure
 */
void* mem_alloc(size_t size, MemCategory category);

/**
 * Allocate zeroed tracked memory
 */
void* mem_calloc(size_t count, size_t size, MemCategory category);

/**
 * Reallocate tracked memory
 */
void* mem_realloc(void* ptr, size_t new_size, MemCategory category);

/**
 * Free tracked memory
 */
void mem_free(void* ptr);

/**
 * Duplicate string with tracking
 */
char* mem_strdup(const char* str, MemCategory category);

/**
 * Duplicate string with length limit
 */
char* mem_strndup(const char* str, size_t max_len, MemCategory category);

// ============================================================================
// Debug Allocation API (with source location)
// ============================================================================

#ifdef MEMTRACK_DEBUG_LOCATIONS

void* mem_alloc_loc(size_t size, MemCategory category, const char* file, int line);
void* mem_calloc_loc(size_t count, size_t size, MemCategory category, const char* file, int line);
void* mem_realloc_loc(void* ptr, size_t new_size, MemCategory category, const char* file, int line);
void mem_free_loc(void* ptr, const char* file, int line);

#define mem_alloc(size, cat)           mem_alloc_loc(size, cat, __FILE__, __LINE__)
#define mem_calloc(count, size, cat)   mem_calloc_loc(count, size, cat, __FILE__, __LINE__)
#define mem_realloc(ptr, size, cat)    mem_realloc_loc(ptr, size, cat, __FILE__, __LINE__)
#define mem_free(ptr)                  mem_free_loc(ptr, __FILE__, __LINE__)

#endif

// ============================================================================
// Typed Allocation API (with TypeMeta)
// ============================================================================

// Forward declaration of TypeMeta from typemeta.h
typedef struct TypeMeta TypeMeta;

/**
 * Allocate memory with type metadata
 * @param type TypeMeta describing the type
 * @param category Memory category for tracking
 * @return Pointer to allocated memory, or NULL on failure
 */
void* mem_alloc_typed(const TypeMeta* type, MemCategory category);

/**
 * Allocate zeroed memory with type metadata
 */
void* mem_calloc_typed(const TypeMeta* type, MemCategory category);

/**
 * Allocate array of typed elements
 * @param element_type TypeMeta for array element type
 * @param count Number of elements
 * @param category Memory category
 */
void* mem_alloc_array_typed(const TypeMeta* element_type, size_t count, MemCategory category);

/**
 * Get type metadata for an allocation
 * @param ptr Pointer to allocated memory
 * @return TypeMeta pointer or NULL if not tracked or no type info
 */
const TypeMeta* mem_get_type(void* ptr);

/**
 * Convenience macros for typed allocation
 * Requires TYPEMETA_<TypeName> to be defined (from typemeta.h)
 */
#define MEM_NEW(type, category) \
    ((type*)mem_alloc_typed(&TYPEMETA_##type, category))

#define MEM_NEW_ZEROED(type, category) \
    ((type*)mem_calloc_typed(&TYPEMETA_##type, category))

#define MEM_NEW_ARRAY(type, count, category) \
    ((type*)mem_alloc_array_typed(&TYPEMETA_##type, count, category))

// ============================================================================
// Query API
// ============================================================================

/**
 * Get current statistics
 */
void memtrack_get_stats(MemtrackStats* stats);

/**
 * Get stats for a specific category
 */
void memtrack_get_category_stats(MemCategory category, MemtrackCategoryStats* stats);

/**
 * Get allocation info for a pointer (debug mode only)
 * @param ptr Pointer to query
 * @param out_size Output: allocation size
 * @param out_category Output: allocation category
 * @return true if pointer is tracked
 */
bool memtrack_get_alloc_info(void* ptr, size_t* out_size, MemCategory* out_category);

/**
 * Check if a pointer is currently allocated (debug mode only)
 */
bool memtrack_is_allocated(void* ptr);

/**
 * Get current total memory usage
 */
size_t memtrack_get_current_usage(void);

/**
 * Get peak memory usage
 */
size_t memtrack_get_peak_usage(void);

/**
 * Get memory usage for a category
 */
size_t memtrack_get_category_usage(MemCategory category);

// ============================================================================
// Memory Pressure API
// ============================================================================

/**
 * Register a memory pressure callback
 * @param callback Function to call when memory pressure is detected
 * @param user_data Context passed to callback
 * @param categories Bitmask of categories this callback can free (0 = all)
 * @return Handle for unregistering, or 0 on failure
 */
uint32_t memtrack_register_pressure_callback(
    MemPressureCallback callback,
    void* user_data,
    uint64_t categories
);

/**
 * Unregister a pressure callback
 */
void memtrack_unregister_pressure_callback(uint32_t handle);

/**
 * Set memory limits for pressure detection
 * @param soft_limit Bytes at which LOW pressure starts
 * @param hard_limit Bytes at which HIGH pressure starts
 * @param critical_limit Bytes at which CRITICAL pressure starts
 */
void memtrack_set_limits(size_t soft_limit, size_t hard_limit, size_t critical_limit);

/**
 * Get current memory pressure level
 */
MemPressureLevel memtrack_get_pressure_level(void);

/**
 * Manually trigger memory pressure handling
 * @param level Pressure level to simulate
 * @return Total bytes freed
 */
size_t memtrack_trigger_pressure(MemPressureLevel level);

/**
 * Request freeing a specific amount of memory
 * @param bytes_needed Bytes to try to free
 * @return Bytes actually freed
 */
size_t memtrack_request_free(size_t bytes_needed);

// ============================================================================
// Debugging / Profiling API
// ============================================================================

/**
 * Print current memory usage to log
 */
void memtrack_log_usage(void);

/**
 * Print detailed allocation report (debug mode only)
 */
void memtrack_log_allocations(void);

/**
 * Print allocations for a specific category
 */
void memtrack_log_category(MemCategory category);

/**
 * Check for memory leaks (returns count)
 */
size_t memtrack_check_leaks(void);

/**
 * Verify all guard bytes are intact (debug mode)
 * @return Number of corrupted allocations found
 */
size_t memtrack_verify_guards(void);

/**
 * Take a memory snapshot for comparison
 * @return Snapshot handle
 */
uint32_t memtrack_snapshot(void);

/**
 * Compare current state to snapshot, log differences
 */
void memtrack_compare_snapshot(uint32_t snapshot_handle);

/**
 * Free a snapshot
 */
void memtrack_free_snapshot(uint32_t snapshot_handle);

/**
 * Enable/disable allocation tracking for current thread
 * Useful for ignoring allocations in certain code paths
 */
void memtrack_thread_enable(bool enable);

// ============================================================================
// Memory Walking API (requires TypeMeta)
// ============================================================================

/**
 * Callback for walking memory allocations
 * @param ptr Pointer to current object
 * @param type Type metadata (NULL if not available)
 * @param size Allocation size
 * @param category Memory category
 * @param user_data User context
 * @return true to continue walking, false to stop
 */
typedef bool (*MemWalkCallback)(
    void* ptr,
    const TypeMeta* type,
    size_t size,
    MemCategory category,
    void* user_data
);

/**
 * Walk all tracked allocations
 * @param callback Function to call for each allocation
 * @param user_data User context
 */
void mem_walk_all(MemWalkCallback callback, void* user_data);

/**
 * Walk allocations of a specific type
 * @param type TypeMeta to filter by
 * @param callback Function to call for each allocation
 * @param user_data User context
 */
void mem_walk_type(const TypeMeta* type, MemWalkCallback callback, void* user_data);

/**
 * Walk allocations in a specific category
 * @param category Category to filter by
 * @param callback Function to call for each allocation
 * @param user_data User context
 */
void mem_walk_category(MemCategory category, MemWalkCallback callback, void* user_data);

/**
 * Validate all typed allocations against their TypeMeta
 * @return Number of invalid allocations found
 */
size_t mem_validate_all_typed(void);

/**
 * Dump allocation information to file
 * @param ptr Allocation to dump
 * @param out Output file
 */
void mem_dump(void* ptr, FILE* out);

/**
 * Dump all allocations to file
 * @param filename Output file path
 */
void mem_dump_all(const char* filename);

/**
 * Export allocation graph in DOT format (for Graphviz)
 * @param filename Output file path
 */
void mem_export_graph(const char* filename);

// ============================================================================
// Integration with existing Pool/Arena allocators
// ============================================================================

// Forward declarations
typedef struct Pool Pool;
typedef struct Arena Arena;

/**
 * Create a tracked pool
 */
Pool* memtrack_pool_create(MemCategory category);

/**
 * Allocate from tracked pool
 */
void* memtrack_pool_alloc(Pool* pool, size_t size);

/**
 * Destroy tracked pool (updates stats)
 */
void memtrack_pool_destroy(Pool* pool);

/**
 * Create a tracked arena
 */
Arena* memtrack_arena_create(Pool* pool, MemCategory category);

/**
 * Allocate from tracked arena
 */
void* memtrack_arena_alloc(Arena* arena, size_t size);

/**
 * Destroy tracked arena
 */
void memtrack_arena_destroy(Arena* arena);

#ifdef __cplusplus
}
#endif

#endif // MEMTRACK_H
