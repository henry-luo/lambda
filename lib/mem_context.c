#include "mem_context.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
// pthread stubs for Windows using SRWLOCK (matches lib/mempool.c convention)
typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
static inline int pthread_mutex_lock(pthread_mutex_t* m)   { AcquireSRWLockExclusive(m); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t* m) { ReleaseSRWLockExclusive(m); return 0; }
#else
#include <pthread.h>
#endif

// ============================================================================
// Internal structs
// ============================================================================

struct MemNode {
    MemNode*      next;        // toward older nodes (list is newest-first)
    MemNode*      prev;        // toward newer nodes
    void*         allocator;   // identity of the tracked allocator
    MemNode*      parent;      // backing allocator's node (may be in another ctx)
    MemContext*   owner;       // owning context
    MemStatFn     stat_fn;
    MemDestroyFn  destroy_fn;
    uint32_t      child_count; // live children backed by this node
    MemKind       kind;
    MemRole       role;
    const char*   label;       // static/long-lived
    uint32_t      id;
    uint32_t      doc_id;
    uint32_t      thread_id;
    uint64_t      birth_seq;
};

struct MemContext {
    MemNode*     head;          // intrusive list of owned nodes (newest-first)
    MemContext*  parent;
    MemContext*  children;      // child-context list head
    MemContext*  sibling_next;  // next sibling under the same parent
    uint32_t     live_count;    // nodes owned directly by this context
    uint32_t     doc_id;        // document this context represents (0 = none)
    bool         enabled;
    bool         is_root;
};

// Document URL registry entry.
typedef struct MemDocEntry {
    uint32_t doc_id;
    uint32_t parent_doc;
    char*    url;          // owning copy
    uint64_t birth_seq;
    bool     live;
} MemDocEntry;

// ============================================================================
// Global state — single process-wide mutex (non-recursive).
// ============================================================================

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static MemContext*     g_root = NULL;
static uint32_t        g_next_id = 1;
static uint64_t        g_birth = 0;

// Thread-local guard: nonzero while THIS thread is inside cascade teardown.
// During teardown the lock is held and the cascade frees nodes itself, so the
// pool_destroy/arena_destroy release hook (which calls mem_unregister) must be
// a no-op — otherwise it would re-enter the non-recursive lock (deadlock) and
// double-free the node.
static __thread int g_in_teardown = 0;

// Document registry (dynamic array).
static MemDocEntry* g_docs = NULL;
static uint32_t     g_doc_count = 0;
static uint32_t     g_doc_cap = 0;
static uint32_t     g_next_doc_id = 1;

#define LOCK()   pthread_mutex_lock(&g_mutex)
#define UNLOCK() pthread_mutex_unlock(&g_mutex)

// ============================================================================
// Small utilities
// ============================================================================

static uint32_t current_thread_id(void) {
#ifdef _WIN32
    return (uint32_t)GetCurrentThreadId();
#else
    pthread_t t = pthread_self();
    // FNV-1a over the opaque pthread_t bytes -> 32-bit id
    uint64_t h = 1469598103934665603ULL;
    const unsigned char* p = (const unsigned char*)&t;
    for (size_t i = 0; i < sizeof(t); i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return (uint32_t)(h ^ (h >> 32));
#endif
}

// Must be called with the lock held.
static MemContext* root_locked(void) {
    if (!g_root) {
        g_root = (MemContext*)calloc(1, sizeof(MemContext));
        if (g_root) {
            g_root->enabled = true;
            g_root->is_root = true;
        }
    }
    return g_root;
}

MemContext* mem_context_root(void) {
    LOCK();
    MemContext* r = root_locked();
    UNLOCK();
    return r;
}

// ============================================================================
// Context lifecycle
// ============================================================================

MemContext* mem_context_create(MemContext* parent, MemRole role, const char* label) {
    (void)role; (void)label;  // reserved for a future synthetic grouping node
    MemContext* ctx = (MemContext*)calloc(1, sizeof(MemContext));
    if (!ctx) return NULL;
    ctx->enabled = true;
    LOCK();
    MemContext* p = parent ? parent : root_locked();
    ctx->parent = p;
    ctx->doc_id = p ? p->doc_id : 0;
    ctx->sibling_next = p->children;
    p->children = ctx;
    UNLOCK();
    return ctx;
}

// Remove `ctx` from its parent's child list. Lock held.
static void detach_from_parent_locked(MemContext* ctx) {
    MemContext* p = ctx->parent;
    if (!p) return;
    MemContext** link = &p->children;
    while (*link) {
        if (*link == ctx) { *link = ctx->sibling_next; break; }
        link = &(*link)->sibling_next;
    }
    ctx->parent = NULL;
    ctx->sibling_next = NULL;
}

// Destroy all nodes + child contexts of `ctx`. Lock held. The root's nodes are
// destroyed but the root struct itself is preserved unless `free_self`.
static void destroy_ctx_locked(MemContext* ctx, bool free_self) {
    // children first (reverse-dependency: children are created after parents)
    MemContext* c = ctx->children;
    while (c) {
        MemContext* nx = c->sibling_next;
        destroy_ctx_locked(c, true);
        c = nx;
    }
    ctx->children = NULL;

    // own nodes, newest-first (head is the most recently registered)
    MemNode* n = ctx->head;
    while (n) {
        MemNode* older = n->next;
        if (n->parent && n->parent->child_count) n->parent->child_count--;
        if (n->destroy_fn) n->destroy_fn(n->allocator);
        free(n);
        n = older;
    }
    ctx->head = NULL;
    ctx->live_count = 0;

    if (free_self && !ctx->is_root) {
        detach_from_parent_locked(ctx);
        free(ctx);
    }
}

void mem_context_destroy(MemContext* ctx) {
    if (!ctx) return;
    LOCK();
    g_in_teardown++;   // suppress the release hook's mem_unregister re-entry
    if (ctx->is_root) {
        // never free the root struct here; just drop its owned graph
        destroy_ctx_locked(ctx, false);
    } else {
        destroy_ctx_locked(ctx, true);
    }
    g_in_teardown--;
    UNLOCK();
}

void mem_context_set_doc_id(MemContext* ctx, uint32_t doc_id) {
    if (!ctx) return;
    LOCK();
    ctx->doc_id = doc_id;
    UNLOCK();
}

uint32_t mem_context_doc_id(MemContext* ctx) {
    if (!ctx) return 0;
    LOCK();
    uint32_t d = ctx->doc_id;
    UNLOCK();
    return d;
}

uint32_t mem_context_live_count(MemContext* ctx) {
    if (!ctx) return 0;
    LOCK();
    uint32_t n = ctx->live_count;
    UNLOCK();
    return n;
}

void mem_context_set_enabled(MemContext* ctx, bool enabled) {
    if (!ctx) return;
    LOCK();
    ctx->enabled = enabled;
    UNLOCK();
}

void mem_context_shutdown(void) {
    LOCK();
    g_in_teardown++;
    if (g_root) {
        destroy_ctx_locked(g_root, false);
        free(g_root);
        g_root = NULL;
    }
    g_in_teardown--;
    for (uint32_t i = 0; i < g_doc_count; i++) free(g_docs[i].url);
    free(g_docs);
    g_docs = NULL;
    g_doc_count = g_doc_cap = 0;
    g_next_doc_id = 1;
    g_next_id = 1;
    g_birth = 0;
    UNLOCK();
}

// ============================================================================
// Node registration
// ============================================================================

MemNode* mem_register(MemContext* ctx, MemKind kind, MemRole role,
                      const char* label, void* allocator, MemNode* parent,
                      MemStatFn stat_fn, MemDestroyFn destroy_fn) {
    if (!ctx) return NULL;
    MemNode* n = (MemNode*)calloc(1, sizeof(MemNode));
    if (!n) return NULL;
    LOCK();
    if (!ctx->enabled) { UNLOCK(); free(n); return NULL; }
    n->allocator = allocator;
    n->parent = parent;
    n->owner = ctx;
    n->stat_fn = stat_fn;
    n->destroy_fn = destroy_fn;
    n->kind = kind;
    n->role = role;
    n->label = label;
    n->id = g_next_id++;
    n->doc_id = ctx->doc_id;
    n->thread_id = current_thread_id();
    n->birth_seq = ++g_birth;
    // push-front onto the owner's list
    n->next = ctx->head;
    n->prev = NULL;
    if (ctx->head) ctx->head->prev = n;
    ctx->head = n;
    ctx->live_count++;
    if (parent) parent->child_count++;
    UNLOCK();
    return n;
}

// Unlink a node from its owning context's list and decrement live_count.
// Does NOT touch child_count or free the node. Lock held.
static void unlink_node_locked(MemNode* node) {
    MemContext* owner = node->owner;
    if (node->prev) node->prev->next = node->next;
    else if (owner) owner->head = node->next;
    if (node->next) node->next->prev = node->prev;
    if (owner && owner->live_count) owner->live_count--;
}

void mem_unregister(MemNode* node) {
    if (!node) return;
    // During cascade teardown the lock is already held by this thread and the
    // cascade frees nodes itself; the release hook must not re-enter here.
    if (g_in_teardown) return;
    LOCK();
    if (node->parent && node->parent->child_count) node->parent->child_count--;
    unlink_node_locked(node);
    UNLOCK();
    free(node);
}

// Is `n` present in the first `count` entries of `arr`?
static bool node_in(MemNode** arr, size_t count, MemNode* n) {
    for (size_t i = 0; i < count; i++) if (arr[i] == n) return true;
    return false;
}

// Unregister `node` AND every allocator whose memory it owns (its transitive
// children by backing/parent edge). Used by the pool/arena release hooks: when
// a pool is bulk-destroyed, the arenas / name-pools / shape-pools whose structs
// live in that pool's memory must be unregistered too, else their nodes dangle
// and a later snapshot reads freed memory.
void mem_unregister_subtree(MemNode* node) {
    if (!node) return;
    if (g_in_teardown) return;   // cascade teardown frees the whole graph itself
    LOCK();
    size_t cap = 8, count = 0;
    MemNode** doomed = (MemNode**)malloc(cap * sizeof(MemNode*));
    if (!doomed) {
        if (node->parent && node->parent->child_count) node->parent->child_count--;
        unlink_node_locked(node);
        UNLOCK();
        free(node);
        return;
    }
    doomed[count++] = node;
    // fixpoint: add any node whose parent is already doomed (BFS over the graph)
    bool changed = true;
    while (changed) {
        changed = false;
        MemContext* stack[64];
        int sp = 0;
        if (g_root) stack[sp++] = g_root;
        while (sp > 0) {
            MemContext* c = stack[--sp];
            for (MemNode* n = c->head; n; n = n->next) {
                if (n->parent && node_in(doomed, count, n->parent) &&
                    !node_in(doomed, count, n)) {
                    if (count == cap) {
                        cap *= 2;
                        MemNode** grown = (MemNode**)realloc(doomed, cap * sizeof(MemNode*));
                        if (!grown) break;   // out of memory: stop expanding
                        doomed = grown;
                    }
                    doomed[count++] = n;
                    changed = true;
                }
            }
            for (MemContext* ch = c->children; ch; ch = ch->sibling_next)
                if (sp < 64) stack[sp++] = ch;
        }
    }
    // unlink all; decrement child_count only for parents that survive
    for (size_t i = 0; i < count; i++) {
        MemNode* d = doomed[i];
        if (d->parent && !node_in(doomed, count, d->parent) && d->parent->child_count)
            d->parent->child_count--;
        unlink_node_locked(d);
    }
    UNLOCK();
    for (size_t i = 0; i < count; i++) free(doomed[i]);
    free(doomed);
}

void mem_node_annotate(MemNode* node, MemRole role, const char* label) {
    if (!node) return;
    LOCK();
    node->role = role;
    if (label) node->label = label;
    UNLOCK();
}

void mem_node_set_doc(MemNode* node, uint32_t doc_id) {
    if (!node) return;
    LOCK();
    node->doc_id = doc_id;
    UNLOCK();
}

void mem_reparent(MemNode* node, MemNode* new_parent) {
    if (!node) return;
    LOCK();
    if (node->parent && node->parent->child_count) node->parent->child_count--;
    node->parent = new_parent;
    if (new_parent) new_parent->child_count++;
    UNLOCK();
}

uint32_t mem_node_id(const MemNode* node) { return node ? node->id : 0; }

uint32_t mem_node_child_count(const MemNode* node) {
    if (!node) return 0;
    LOCK();
    uint32_t c = node->child_count;
    UNLOCK();
    return c;
}

void* mem_node_allocator(const MemNode* node) { return node ? node->allocator : NULL; }

// ============================================================================
// Document URL registry
// ============================================================================

// Lock held.
static MemDocEntry* doc_find_locked(uint32_t doc_id) {
    for (uint32_t i = 0; i < g_doc_count; i++)
        if (g_docs[i].doc_id == doc_id) return &g_docs[i];
    return NULL;
}

uint32_t mem_doc_register(const char* url, uint32_t parent_doc) {
    LOCK();
    if (g_doc_count == g_doc_cap) {
        uint32_t ncap = g_doc_cap ? g_doc_cap * 2 : 8;
        MemDocEntry* nd = (MemDocEntry*)realloc(g_docs, ncap * sizeof(MemDocEntry));
        if (!nd) { UNLOCK(); return 0; }
        g_docs = nd;
        g_doc_cap = ncap;
    }
    MemDocEntry* e = &g_docs[g_doc_count++];
    e->doc_id = g_next_doc_id++;
    e->parent_doc = parent_doc;
    e->url = url ? strdup(url) : NULL;
    e->birth_seq = ++g_birth;
    e->live = true;
    uint32_t id = e->doc_id;
    UNLOCK();
    return id;
}

const char* mem_doc_url(uint32_t doc_id) {
    if (doc_id == 0) return NULL;
    LOCK();
    MemDocEntry* e = doc_find_locked(doc_id);
    const char* u = e ? e->url : NULL;
    UNLOCK();
    return u;
}

uint32_t mem_doc_parent(uint32_t doc_id) {
    if (doc_id == 0) return 0;
    LOCK();
    MemDocEntry* e = doc_find_locked(doc_id);
    uint32_t p = e ? e->parent_doc : 0;
    UNLOCK();
    return p;
}

void mem_doc_retire(uint32_t doc_id) {
    if (doc_id == 0) return;
    LOCK();
    MemDocEntry* e = doc_find_locked(doc_id);
    if (e) e->live = false;
    UNLOCK();
}

// ============================================================================
// Snapshot
// ============================================================================

// Count nodes in ctx + descendants. Lock held.
static uint32_t count_nodes_locked(MemContext* ctx) {
    uint32_t n = ctx->live_count;
    for (MemContext* c = ctx->children; c; c = c->sibling_next)
        n += count_nodes_locked(c);
    return n;
}

// Fill one sample from a node. Lock held.
static void fill_sample_locked(MemNode* n, MemStatSample* s) {
    memset(s, 0, sizeof(*s));
    s->id = n->id;
    s->parent_id = n->parent ? n->parent->id : 0;
    s->doc_id = n->doc_id;
    s->kind = (uint16_t)n->kind;
    s->role = (uint16_t)n->role;
    s->thread_id = n->thread_id;
    s->birth_seq = n->birth_seq;
    if (n->child_count) s->flags |= MEM_FLAG_CHILDREN;
    if (n->label) {
        strncpy(s->label, n->label, sizeof(s->label) - 1);
        s->label[sizeof(s->label) - 1] = '\0';
    }
    // dynamic fields come from the allocator's own counters
    if (n->stat_fn) n->stat_fn(n->allocator, s);
}

// Emit ctx + descendants into samples[], advancing *idx. Lock held.
static void emit_samples_locked(MemContext* ctx, MemStatSample* samples,
                                uint32_t* idx, uint32_t cap) {
    for (MemNode* n = ctx->head; n; n = n->next) {
        if (*idx >= cap) return;
        fill_sample_locked(n, &samples[(*idx)++]);
    }
    for (MemContext* c = ctx->children; c; c = c->sibling_next)
        emit_samples_locked(c, samples, idx, cap);
}

MemSnapshot* mem_snapshot_capture(MemContext* ctx) {
    if (!ctx) ctx = mem_context_root();
    MemSnapshot* snap = (MemSnapshot*)calloc(1, sizeof(MemSnapshot));
    if (!snap) return NULL;
    LOCK();
    uint32_t n = count_nodes_locked(ctx);
    MemStatSample* samples = NULL;
    if (n > 0) {
        samples = (MemStatSample*)calloc(n, sizeof(MemStatSample));
        if (!samples) { UNLOCK(); free(snap); return NULL; }
        uint32_t idx = 0;
        emit_samples_locked(ctx, samples, &idx, n);
        n = idx;  // actual filled count
    }
    snap->captured_seq = g_birth;
    snap->count = n;
    snap->samples = samples;
    // A pool's live count includes the exact backing allocations of its child
    // arenas. Subtract those bytes only for logical attribution; physical
    // totals below continue to count each independent root allocator once.
    for (uint32_t child = 0; child < n; child++) {
        uint32_t parent_id = samples[child].parent_id;
        uint64_t backing = samples[child].backing_bytes;
        if (!parent_id || !backing) continue;
        for (uint32_t parent = 0; parent < n; parent++) {
            if (samples[parent].id != parent_id) continue;
            if (backing > samples[parent].direct_bytes) {
                samples[parent].flags |= MEM_FLAG_ATTRIBUTION_ERROR;
                log_error("MEMCTX-ATTRIBUTION: child #%u backing=%llu exceeds parent #%u direct=%llu",
                          samples[child].id, (unsigned long long)backing,
                          samples[parent].id,
                          (unsigned long long)samples[parent].direct_bytes);
                assert(backing <= samples[parent].direct_bytes);
            } else {
                samples[parent].direct_bytes -= backing;
            }
            break;
        }
    }

    uint64_t tr = 0, tu = 0, ptr = 0, ptu = 0;
    for (uint32_t i = 0; i < n; i++) {
        tr += samples[i].bytes_reserved;
        tu += samples[i].bytes_in_use;
        if (samples[i].parent_id == 0) {
            ptr += samples[i].bytes_reserved;
            ptu += samples[i].bytes_in_use;
        }
    }
    snap->total_reserved = tr;
    snap->total_in_use = tu;
    snap->physical_total_reserved = ptr;
    snap->physical_total_in_use = ptu;
    UNLOCK();
    return snap;
}

void mem_snapshot_free(MemSnapshot* snap) {
    if (!snap) return;
    free(snap->samples);
    free(snap);
}

// ---- JSON serialization (tiny growable buffer; no external deps) ----

typedef struct { char* buf; size_t len; size_t cap; } JBuf;

static bool jb_reserve(JBuf* j, size_t extra) {
    if (j->len + extra + 1 <= j->cap) return true;
    size_t ncap = j->cap ? j->cap : 256;
    while (j->len + extra + 1 > ncap) ncap *= 2;
    char* nb = (char*)realloc(j->buf, ncap);
    if (!nb) return false;
    j->buf = nb; j->cap = ncap;
    return true;
}

static void jb_raw(JBuf* j, const char* s, size_t n) {
    if (!jb_reserve(j, n)) return;
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void jb_puts(JBuf* j, const char* s) { jb_raw(j, s, strlen(s)); }

static void jb_fmt(JBuf* j, const char* fmt, ...) {
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) { jb_raw(j, tmp, (size_t)n); return; }
    // rare: format longer than tmp
    if (!jb_reserve(j, (size_t)n)) return;
    va_start(ap, fmt);
    vsnprintf(j->buf + j->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    j->len += (size_t)n;
}

static void jb_json_str(JBuf* j, const char* s) {
    jb_puts(j, "\"");
    if (s) {
        for (const char* p = s; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '"':  jb_puts(j, "\\\""); break;
                case '\\': jb_puts(j, "\\\\"); break;
                case '\n': jb_puts(j, "\\n");  break;
                case '\r': jb_puts(j, "\\r");  break;
                case '\t': jb_puts(j, "\\t");  break;
                default:
                    if (c < 0x20) jb_fmt(j, "\\u%04x", c);
                    else jb_raw(j, (const char*)&c, 1);
            }
        }
    }
    jb_puts(j, "\"");
}

static void jb_mem_sample(JBuf* j, const MemStatSample* s) {
    jb_fmt(j, "{\"id\":%u,\"parent_id\":%u,\"doc_id\":%u,",
           s->id, s->parent_id, s->doc_id);
    jb_puts(j, "\"kind\":");
    jb_json_str(j, mem_kind_name((MemKind)s->kind));
    jb_puts(j, ",\"role\":");
    jb_json_str(j, mem_role_name((MemRole)s->role));
    jb_puts(j, ",\"label\":");
    jb_json_str(j, s->label);
    jb_puts(j, ",\"url\":");
    jb_json_str(j, mem_doc_url(s->doc_id));
    jb_fmt(j, ",\"bytes_reserved\":%llu,\"bytes_in_use\":%llu,\"backing_bytes\":%llu,\"direct_bytes\":%llu,\"committed_bytes\":%llu,\"recyclable_bytes\":%llu,\"waste_bytes\":%llu,\"overhead_bytes\":%llu,\"high_water_bytes\":%llu,\"cumulative_bytes\":%llu,\"alloc_count\":%llu,\"free_count\":%llu,\"reuse_hits\":%llu,\"reuse_misses\":%llu,\"split_count\":%llu,\"coalesce_count\":%llu,\"bump_back_count\":%llu,\"fresh_chunk_count\":%llu,\"fresh_growth_bytes\":%llu,\"reset_count\":%llu,\"clear_count\":%llu,\"active_scope_count\":%u,\"chunk_count\":%u,\"thread\":%u,\"flags\":%u}",
           (unsigned long long)s->bytes_reserved,
           (unsigned long long)s->bytes_in_use,
           (unsigned long long)s->backing_bytes,
           (unsigned long long)s->direct_bytes,
           (unsigned long long)s->committed_bytes,
           (unsigned long long)s->recyclable_bytes,
           (unsigned long long)s->waste_bytes,
           (unsigned long long)s->overhead_bytes,
           (unsigned long long)s->high_water_bytes,
           (unsigned long long)s->cumulative_bytes,
           (unsigned long long)s->alloc_count,
           (unsigned long long)s->free_count,
           (unsigned long long)s->reuse_hits,
           (unsigned long long)s->reuse_misses,
           (unsigned long long)s->split_count,
           (unsigned long long)s->coalesce_count,
           (unsigned long long)s->bump_back_count,
           (unsigned long long)s->fresh_chunk_count,
           (unsigned long long)s->fresh_growth_bytes,
           (unsigned long long)s->reset_count,
           (unsigned long long)s->clear_count,
           s->active_scope_count, s->chunk_count, s->thread_id, s->flags);
}

char* mem_snapshot_to_json(const MemSnapshot* snap) {
    if (!snap) return NULL;
    JBuf j = {0};
    jb_fmt(&j, "{\"captured_seq\":%llu,\"count\":%u,\"total_reserved\":%llu,\"total_in_use\":%llu,\"physical_total\":{\"bytes_reserved\":%llu,\"bytes_in_use\":%llu},\"nodes\":[",
           (unsigned long long)snap->captured_seq, snap->count,
           (unsigned long long)snap->total_reserved,
           (unsigned long long)snap->total_in_use,
           (unsigned long long)snap->physical_total_reserved,
           (unsigned long long)snap->physical_total_in_use);
    for (uint32_t i = 0; i < snap->count; i++) {
        if (i) jb_puts(&j, ",");
        jb_mem_sample(&j, &snap->samples[i]);
    }
    jb_puts(&j, "],\"logical_domains\":[");
    for (uint32_t i = 0; i < snap->count; i++) {
        if (i) jb_puts(&j, ",");
        jb_mem_sample(&j, &snap->samples[i]);
    }
    jb_puts(&j, "]}");
    return j.buf;  // may be NULL if nothing was written and no alloc happened
}

void mem_context_log(MemContext* ctx) {
    MemSnapshot* snap = mem_snapshot_capture(ctx);
    if (!snap) return;
    log_info("MEMCTX: snapshot seq=%llu nodes=%u reserved=%llu in_use=%llu",
             (unsigned long long)snap->captured_seq, snap->count,
             (unsigned long long)snap->total_reserved,
             (unsigned long long)snap->total_in_use);
    for (uint32_t i = 0; i < snap->count; i++) {
        const MemStatSample* s = &snap->samples[i];
        const char* url = mem_doc_url(s->doc_id);
        log_info("MEMCTX:   #%u parent=%u doc=%u(%s) %s/%s '%s' reserved=%llu in_use=%llu allocs=%llu",
                 s->id, s->parent_id, s->doc_id, url ? url : "-",
                 mem_kind_name((MemKind)s->kind), mem_role_name((MemRole)s->role),
                 s->label, (unsigned long long)s->bytes_reserved,
                 (unsigned long long)s->bytes_in_use,
                 (unsigned long long)s->alloc_count);
    }
    mem_snapshot_free(snap);
}

bool mem_context_dump_json_file(MemContext* ctx, const char* path) {
    if (!path) return false;
    MemSnapshot* snap = mem_snapshot_capture(ctx);
    if (!snap) return false;
    char* json = mem_snapshot_to_json(snap);
    bool ok = false;
    if (json) {
        FILE* f = fopen(path, "w");
        if (f) {
            size_t len = strlen(json);
            ok = (fwrite(json, 1, len, f) == len);
            fclose(f);
            log_info("MEMCTX: dumped %u allocator(s) to %s", snap->count, path);
        } else {
            log_error("MEMCTX: could not open '%s' for memory dump", path);
        }
        free(json);
    }
    mem_snapshot_free(snap);
    return ok;
}

uint32_t mem_context_report_leaks(MemContext* ctx) {
    MemSnapshot* snap = mem_snapshot_capture(ctx);
    if (!snap) return 0;
    uint32_t n = snap->count;
    if (n == 0) {
        log_info("MEMCTX-LEAK: no live allocators (clean)");
    } else {
        log_info("MEMCTX-LEAK: %u allocator(s) still live (reserved=%llu in_use=%llu)",
                 n, (unsigned long long)snap->total_reserved,
                 (unsigned long long)snap->total_in_use);
        for (uint32_t i = 0; i < n; i++) {
            const MemStatSample* s = &snap->samples[i];
            const char* url = mem_doc_url(s->doc_id);
            log_info("MEMCTX-LEAK:   #%u %s/%s '%s' doc=%u(%s) reserved=%llu in_use=%llu",
                     s->id, mem_kind_name((MemKind)s->kind),
                     mem_role_name((MemRole)s->role), s->label,
                     s->doc_id, url ? url : "-",
                     (unsigned long long)s->bytes_reserved,
                     (unsigned long long)s->bytes_in_use);
        }
    }
    mem_snapshot_free(snap);
    return n;
}

// ============================================================================
// Name helpers
// ============================================================================

const char* mem_kind_name(MemKind k) {
    switch (k) {
        case MEM_KIND_POOL:      return "pool";
        case MEM_KIND_ARENA:     return "arena";
        case MEM_KIND_SCRATCH:   return "scratch";
        case MEM_KIND_HEAP:      return "heap";
        case MEM_KIND_NAMEPOOL:  return "namepool";
        case MEM_KIND_SHAPEPOOL: return "shapepool";
        case MEM_KIND_JIT:       return "jit";
        case MEM_KIND_CACHE:     return "cache";
        case MEM_KIND_CONTEXT:   return "context";
        default:                 return "unknown";
    }
}

const char* mem_role_name(MemRole r) {
    switch (r) {
        case MEM_ROLE_INPUT:        return "input";
        case MEM_ROLE_AST:          return "ast";
        case MEM_ROLE_TYPE_SHAPE:   return "type_shape";
        case MEM_ROLE_VIEW:         return "view";
        case MEM_ROLE_NODE:         return "node";
        case MEM_ROLE_LAYOUT:       return "layout";
        case MEM_ROLE_RENDER:       return "render";
        case MEM_ROLE_FONT:         return "font";
        case MEM_ROLE_CSS:          return "css";
        case MEM_ROLE_PDF:          return "pdf";
        case MEM_ROLE_VALIDATOR:    return "validator";
        case MEM_ROLE_RUNTIME_HEAP: return "runtime_heap";
        case MEM_ROLE_CODE:         return "code";
        case MEM_ROLE_MEDIA:        return "media";
        case MEM_ROLE_NETWORK:      return "network";
        case MEM_ROLE_TEMP:         return "temp";
        default:                    return "unknown";
    }
}
