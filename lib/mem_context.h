#ifndef MEM_CONTEXT_H
#define MEM_CONTEXT_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memory Context — central allocator registry & introspection (Stage 1 core).
 *
 * The Memory Context is the single rooted owner of every allocator in the
 * process. Allocators (pools, arenas, scratch arenas, GC heaps, JIT code,
 * caches) link in as MemNode records carrying their kind, semantic role,
 * label, owning document, and backing (parent) edge. The context can then:
 *   - take a cheap flat binary snapshot of the live allocator graph,
 *   - cascade-destroy a whole subtree in dependency order,
 *   - attribute memory to the document (URL) it belongs to.
 *
 * This core is intentionally allocator-agnostic: each node carries optional
 * stat/destroy callbacks, so the core never needs to include any allocator
 * header. Factory wrappers (mem_pool_create, mem_arena_create, …) layer on top
 * in later phases.
 *
 * THREAD SAFETY: all public functions are guarded by a single process-global
 * mutex. Allocator *creation/destruction* is infrequent, so this is not on any
 * hot allocation path. Callbacks (MemStatFn/MemDestroyFn) MUST NOT call back
 * into any mem_context_* function — the mutex is non-recursive.
 */

// Kind of allocator a node tracks.
typedef enum MemKind {
    MEM_KIND_POOL = 1,     // lib/mempool.c  (rpmalloc or mmap)
    MEM_KIND_ARENA,        // lib/arena.c
    MEM_KIND_SCRATCH,      // lib/scratch_arena.c
    MEM_KIND_HEAP,         // gc_heap (Lambda runtime objects)
    MEM_KIND_NAMEPOOL,     // interned names
    MEM_KIND_SHAPEPOOL,    // cached shapes
    MEM_KIND_JIT,          // MIR-generated executable code (mmap'd code pages)
    MEM_KIND_CACHE,        // self-managed cache (hashmap + LRU): font / image / vector
    MEM_KIND_CONTEXT,      // a sub-context grouping node (synthetic)
} MemKind;

// Semantic role — what the allocator is for.
typedef enum MemRole {
    MEM_ROLE_UNKNOWN = 0,
    MEM_ROLE_INPUT,        // input parser (JSON/XML/HTML/…)
    MEM_ROLE_AST,          // code AST (Lambda / Ruby / Python / JS)
    MEM_ROLE_TYPE_SHAPE,   // TypeMap / ShapeEntry / type metadata
    MEM_ROLE_VIEW,         // Radiant view tree
    MEM_ROLE_NODE,         // DOM nodes
    MEM_ROLE_LAYOUT,       // layout scratch
    MEM_ROLE_RENDER,       // render scratch / pixel buffers
    MEM_ROLE_FONT,         // glyphs, font metadata
    MEM_ROLE_CSS,          // CSS engine / stylesheets
    MEM_ROLE_PDF,          // PDF parsing / view
    MEM_ROLE_VALIDATOR,    // schema validation
    MEM_ROLE_RUNTIME_HEAP, // GC heap / nursery
    MEM_ROLE_CODE,         // MIR/JIT compiled code + debug_info
    MEM_ROLE_MEDIA,        // decoded image / audio / video buffers
    MEM_ROLE_NETWORK,      // download buffers / network resources
    MEM_ROLE_TEMP,         // short-lived scratch
    MEM_ROLE_COUNT
} MemRole;

// Opaque handles.
typedef struct MemContext MemContext;
typedef struct MemNode MemNode;

// Forward declaration so the callback typedef can reference it.
typedef struct MemStatSample MemStatSample;

// Stat callback: fill the dynamic fields (bytes_reserved/bytes_in_use/
// alloc_count/chunk_count and any extra flags) of an already identity-filled
// sample. Return false if the allocator is no longer valid. MUST NOT call any
// mem_context_* function.
typedef bool (*MemStatFn)(void* allocator, MemStatSample* out);

// Destroy callback: release the underlying allocator. Called during cascade
// teardown. MUST NOT call any mem_context_* function.
typedef void (*MemDestroyFn)(void* allocator);

// Snapshot flags.
#define MEM_FLAG_MMAP      0x01u   // allocator is mmap-backed
#define MEM_FLAG_DRAINED   0x02u   // allocator drained/invalidated
#define MEM_FLAG_LEAKED    0x04u   // outlived its owner (set by leak report)
#define MEM_FLAG_CHILDREN  0x08u   // has live children backed by it
#define MEM_FLAG_ATTRIBUTION_ERROR 0x10u // child backing exceeded parent live bytes

// Flat, fixed-width, copyable snapshot record (no live pointers — ids only).
struct MemStatSample {
    uint32_t id;
    uint32_t parent_id;        // backing allocator's node id (0 = root-level)
    uint32_t doc_id;           // owning document (0 = not doc-scoped)
    uint16_t kind;             // MemKind
    uint16_t role;             // MemRole
    uint32_t thread_id;        // creating thread
    uint32_t chunk_count;
    uint64_t birth_seq;        // creation order
    uint64_t bytes_reserved;   // capacity held from OS
    uint64_t bytes_in_use;     // live/bump-used bytes (best effort)
    uint64_t backing_bytes;    // exact bytes this allocator occupies in its parent
    uint64_t direct_bytes;     // logical live bytes excluding child allocator backing
    uint64_t committed_bytes;  // allocator capacity available to clients
    uint64_t recyclable_bytes; // inactive bytes available to allocator free bins
    uint64_t waste_bytes;      // committed bytes unavailable to active allocations
    uint64_t overhead_bytes;   // allocator/chunk headers and backing rounding
    uint64_t high_water_bytes; // peak live/active bytes
    uint64_t cumulative_bytes; // cumulative requested allocation bytes
    uint64_t alloc_count;      // cumulative allocations (upper bound for rpmalloc)
    uint64_t free_count;
    uint64_t reuse_hits;
    uint64_t reuse_misses;
    uint64_t split_count;
    uint64_t coalesce_count;
    uint64_t bump_back_count;
    uint64_t fresh_chunk_count;
    uint64_t fresh_growth_bytes;
    uint64_t reset_count;
    uint64_t clear_count;
    uint32_t active_scope_count;
    uint32_t flags;            // MEM_FLAG_*
    char     label[44];        // copied, truncated — self-contained for offline view
};

// Immutable detached snapshot.
typedef struct MemSnapshot {
    uint64_t       captured_seq;
    uint32_t       count;
    uint32_t       _pad;
    MemStatSample* samples;        // `count` entries
    uint64_t       total_reserved;
    uint64_t       total_in_use;
    // Physical totals include independent root-level allocators once. Logical
    // child samples stay visible without double-counting their parent backing.
    uint64_t       physical_total_reserved;
    uint64_t       physical_total_in_use;
} MemSnapshot;

// ---- Context lifecycle ----

// The process-global root context (lazily created).
MemContext* mem_context_root(void);

// Create a sub-context linked under `parent` (NULL → root). A sub-context
// groups allocators (typically per-document or per-pass) for bulk teardown.
MemContext* mem_context_create(MemContext* parent, MemRole role, const char* label);

// Cascade-destroy: destroys all child contexts first, then this context's
// owned allocators in reverse-birth (children-before-parent) order, invoking
// each node's MemDestroyFn. Safe on NULL. Does nothing to the root.
void mem_context_destroy(MemContext* ctx);

// Associate a document id with a context; allocators created afterward inherit
// it. See mem_doc_register().
void     mem_context_set_doc_id(MemContext* ctx, uint32_t doc_id);
uint32_t mem_context_doc_id(MemContext* ctx);

// Number of allocators owned directly by this context (excludes children).
uint32_t mem_context_live_count(MemContext* ctx);

// Enable/disable tracking for a context. When disabled, mem_register returns
// NULL (callers treat a NULL node as "untracked"). Default: enabled.
void mem_context_set_enabled(MemContext* ctx, bool enabled);

// Tear down the root context and the document registry. For clean process
// shutdown (Valgrind/ASan). After this, mem_context_root() recreates a fresh
// root. Optional.
void mem_context_shutdown(void);

// ---- Node registration ----

// Register an allocator under `ctx`. `label` must be a static/long-lived
// string. `parent` is the backing allocator's node (NULL = root-level).
// `stat_fn`/`destroy_fn` may be NULL. Returns the node, or NULL if ctx is
// disabled/invalid (callers must tolerate NULL).
MemNode* mem_register(MemContext* ctx, MemKind kind, MemRole role,
                      const char* label, void* allocator, MemNode* parent,
                      MemStatFn stat_fn, MemDestroyFn destroy_fn);

// Unregister and free the node (does NOT destroy the allocator). NULL-safe,
// idempotent.
void mem_unregister(MemNode* node);

// Unregister `node` and every allocator whose memory it backs (its transitive
// children by parent edge). Used by pool/arena release hooks so that bulk
// destroying a pool also unregisters the arenas / name-pools / shape-pools
// whose structs live in that pool's memory. NULL-safe.
void mem_unregister_subtree(MemNode* node);

// Update a node's role/label after creation. NULL-safe.
void mem_node_annotate(MemNode* node, MemRole role, const char* label);

// Set a node's document id. NULL-safe.
void mem_node_set_doc(MemNode* node, uint32_t doc_id);

// Change a node's backing (parent) edge. NULL-safe.
void mem_reparent(MemNode* node, MemNode* new_parent);

uint32_t mem_node_id(const MemNode* node);
uint32_t mem_node_child_count(const MemNode* node);
void*    mem_node_allocator(const MemNode* node);

// ---- Document URL registry (process-global) ----

// Register a document; returns its stable id (>= 1). `url` is copied.
// `parent_doc` links a sub-resource (CSS/JS/font/image) to its owning page
// (0 = top-level document).
uint32_t    mem_doc_register(const char* url, uint32_t parent_doc);
// Resolve an id to its URL (NULL if unknown). Cheap lookup.
const char* mem_doc_url(uint32_t doc_id);
// Resolve a doc's parent document id (0 if top-level/unknown).
uint32_t    mem_doc_parent(uint32_t doc_id);
// Mark a document torn down (keeps the entry for diff/post-mortem; id is never
// reused). NULL-safe.
void        mem_doc_retire(uint32_t doc_id);

// ---- Snapshots ----

// Capture a detached snapshot of `ctx` and all its descendant contexts.
// O(n) under the global lock; the returned copy is immutable and outlives the
// allocators. Returns NULL on allocation failure. Free with mem_snapshot_free.
MemSnapshot* mem_snapshot_capture(MemContext* ctx);
void         mem_snapshot_free(MemSnapshot* snap);

// Serialize a snapshot to a malloc'd JSON string (caller frees). Resolves doc
// URLs via the registry. Returns NULL on failure.
char* mem_snapshot_to_json(const MemSnapshot* snap);

// Log a snapshot of `ctx` as an indented tree via log_info (diagnostics /
// leak report). NULL → root.
void mem_context_log(MemContext* ctx);

// Capture a snapshot of `ctx` (NULL → root) and write it as JSON to `path`.
// Returns true on success. Creates/overwrites the file.
bool mem_context_dump_json_file(MemContext* ctx, const char* path);

// Log every allocator still live under `ctx` (NULL → root) as a potential
// leak, via log_info with a "MEMCTX-LEAK:" prefix. Returns the count of live
// allocators (0 = clean). Intended for a process-exit leak report.
uint32_t mem_context_report_leaks(MemContext* ctx);

// ---- Helpers ----

const char* mem_kind_name(MemKind k);
const char* mem_role_name(MemRole r);

#ifdef __cplusplus
}
#endif

#endif // MEM_CONTEXT_H
