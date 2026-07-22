#include "../transpiler.hpp"
#include <limits.h>
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/str.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap.h"
#include "gc/gc_heap.h"
#include "mem_factory_rt.h"
#include "../lambda-error.h"
#include "../jube/jube_registry.h"
#include "../js/js_runtime.h"
#include "../js/js_exec_profile_weak.h"
#include "../js/js_typed_array.h"
#include "../lambda-decimal.hpp"
#include "lambda-stack.h"
#include "side_stack.h"
#include "../binary.h"
#include <mpdecimal.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern __thread EvalContext* context;
extern "C" Context* _lambda_rt;
extern "C" void js_generator_map_gc_trace(Map* map, gc_heap_t* gc);
extern "C" void js_collection_map_gc_trace(Map* map, gc_heap_t* gc);
extern "C" void js_iterator_map_gc_trace(Map* map, gc_heap_t* gc);

static void gc_finalize_all_objects(gc_heap_t *gc);
extern "C" void vmap_gc_destroy(void* obj, void* data);

static void gc_destroy_external_payload(void* obj, uint16_t type_tag) {
    if (!obj) return;
    if (type_tag == LMD_TYPE_BINARY) {
        Binary* binary = (Binary*)obj;
        if (binary->storage) binary_release_storage(binary);
        return;
    }
    if (type_tag == LMD_TYPE_ARRAY) {
        Array* array = (Array*)obj;
        // Array item buffers live outside the GC zones, so sweep must release
        // them before their dead owners can accumulate across a hot runtime.
        if (array->items && js_array_runtime_items_release(array->items)) {
            array->items = NULL;
            array->capacity = 0;
        }
        return;
    }
    if (type_tag == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* array = (ArrayNum*)obj;
        if (!(array->is_view && array->extra)) return;
        ArrayNumShape* shape = (ArrayNumShape*)(uintptr_t)array->extra;
        if (shape->backing_kind != ARRAY_NUM_BACKING_BYTE_STORAGE || !shape->backing) return;
        // Each retained-storage view owns exactly one reference, including
        // derived views, so sweep and teardown must consume it once.
        ByteStorage* storage = (ByteStorage*)shape->backing;
        shape->backing = NULL;
        byte_storage_release(storage);
    }
}

static void heap_assert_raw_item_allocation(void* ptr, TypeId type_id) {
    (void)type_id;
    if (!ptr) return;
    // Allocation returns before many constructors write byte-zero TypeId, so
    // only the pointer-form invariant is valid at this boundary.
    assert_raw_item_pointer(ptr);
}

static void gc_finalize_vmap_host_payload(VMap* vm) {
    if (!vm || !vm->host_type || !vm->host_data) return;
    const JubeTypeDef* type = jube_find_type_by_host_type(vm->host_type);
    if (!type || !(type->flags & JUBE_TYPE_OWNING_NATIVE)) return;
    // DOM3 declared-interface types carry no host_ops; their finalizer lives on
    // the typedef itself, so fall back to JubeTypeDef.destroy.
    void (*destroy)(void*) = (type->host_ops && type->host_ops->destroy)
        ? type->host_ops->destroy : type->destroy;
    if (!destroy) return;
    void* native = vm->host_data;
    // owning host VMAPs store native payload beside the backing map, so GC must
    // finalize host_data before freeing the backing store or the payload leaks.
    vm->host_data = NULL;
    destroy(native);
}

static void gc_destroy_vmap_object(void* obj, void* data) {
    gc_finalize_vmap_host_payload((VMap*)obj);
    vmap_gc_destroy(obj, data);
}

static JsTypedArray* gc_typed_array_from_map(Map* map) {
    if (!map) return NULL;
    if (map->data_cap == 0) {
        return (JsTypedArray*)map->data;
    }
    bool found = false;
    Item ta_val = js_map_get_fast_ext(map, "__ta__", 6, &found);
    if (found) return (JsTypedArray*)(uintptr_t)it2i(ta_val);
    return NULL;
}

static void js_native_map_gc_trace(void* data, gc_heap_t* gc) {
    Map* map = (Map*)data;
    if (!map || !gc) return;
    js_generator_map_gc_trace(map, gc);
    js_collection_map_gc_trace(map, gc);
    js_iterator_map_gc_trace(map, gc);
    if (map->type_id != LMD_TYPE_MAP || map->map_kind != MAP_KIND_TYPED_ARRAY) return;

    JsTypedArray* ta = gc_typed_array_from_map(map);
    if (ta && ta->buffer_item) {
        gc_mark_item(gc, ta->buffer_item);
    }
    if (ta && ta->view) {
        gc_mark_object_ptr(gc, ta->view);
    }
}

static void gc_finalize_arraybuffer(JsArrayBuffer* ab, gc_native_seen_t* seen_native) {
    if (!ab || gc_native_seen_seen_or_add(seen_native, ab)) return;
    byte_buffer_destroy(&ab->handle);
    mem_free(ab);
}

static void gc_finalize_typed_array(JsTypedArray* ta, gc_native_seen_t* seen_native) {
    if (!ta || gc_native_seen_seen_or_add(seen_native, ta)) return;
    mem_free(ta);
}

extern "C" void js_regex_map_heap_destroy(Map* map, gc_native_seen_t* seen_native);
extern "C" void js_collection_map_heap_destroy(Map* map, gc_native_seen_t* seen_native);

static void gc_finalize_js_native_map(Map* map, gc_native_seen_t* seen_native) {
    if (!map) return;
    // JS collections keep their native HashMap in a pool-owned side record; the
    // heap finalizer must release it before the pool record disappears.
    js_collection_map_heap_destroy(map, seen_native);
    switch (map->map_kind) {
    case MAP_KIND_TYPED_ARRAY: {
        JsTypedArray* ta = gc_typed_array_from_map(map);
        gc_finalize_typed_array(ta, seen_native);
        map->data = NULL;
        break;
    }
    case MAP_KIND_ARRAYBUFFER: {
        JsArrayBuffer* ab = NULL;
        if (map->data_cap == 0) {
            ab = (JsArrayBuffer*)map->data;
        } else {
            bool found = false;
            Item ab_val = js_map_get_fast_ext(map, "__ab__", 6, &found);
            if (found) ab = (JsArrayBuffer*)(uintptr_t)it2i(ab_val);
        }
        gc_finalize_arraybuffer(ab, seen_native);
        map->data = NULL;
        break;
    }
    case MAP_KIND_DATAVIEW: {
        JsDataView* dv = NULL;
        if (map->data_cap == 0) {
            dv = (JsDataView*)map->data;
        } else {
            bool found = false;
            Item dv_val = js_map_get_fast_ext(map, "__dv__", 6, &found);
            if (found) dv = (JsDataView*)(uintptr_t)it2i(dv_val);
        }
        if (dv && !gc_native_seen_seen_or_add(seen_native, dv)) mem_free(dv);
        map->data = NULL;
        break;
    }
    case MAP_KIND_ITERATOR:
    case MAP_KIND_PROXY:
        if (map->data && !gc_native_seen_seen_or_add(seen_native, map->data)) {
            mem_free(map->data);
        }
        map->data = NULL;
        break;
    case MAP_KIND_ARRAY_SPARSE: {
        SparseArrayMap* sm = (SparseArrayMap*)map;
        if (sm->sparse_indices && !gc_native_seen_seen_or_add(seen_native, sm->sparse_indices)) {
            hashmap_free(sm->sparse_indices);
        }
        sm->sparse_indices = NULL;
        break;
    }
    default:
        break;
    }
    js_regex_map_heap_destroy(map, seen_native);
}

// VMap GC bridge functions (defined in vmap.cpp)
extern "C" void vmap_gc_trace(void* data, gc_heap_t* gc);
extern "C" void err_gc_trace(void* data, gc_heap_t* gc);
extern "C" void err_gc_destroy(void* data);
extern "C" int js_function_gc_trace(void* data, gc_heap_t* gc);
extern "C" int js_function_gc_compact(void* data, gc_heap_t* gc);
Item js_map_get_fast_ext(Map* m, const char* key_str, int key_len, bool* out_found);

// ── Interned single-char ASCII strings (Optimization 4) ──────────────
// Pre-allocated table of 128 String objects for ASCII chars 0-127.
// These are statically allocated (not GC-managed) and never freed.
// Each entry is sizeof(String) + 2 bytes (1 char + null terminator).
#define ASCII_CHAR_ENTRY_SIZE (sizeof(String) + 2)
static char ascii_char_storage[128 * ASCII_CHAR_ENTRY_SIZE];
static String* ascii_char_table[128];
static bool ascii_char_table_initialized = false;

static void heap_configure_gc_force_schedule(gc_heap_t* gc) {
    if (!gc) return;
    const char* requested = getenv("LAMBDA_GC_FORCE_EVERY");
    if (requested && requested[0]) {
        errno = 0;
        char* end = NULL;
        unsigned long long parsed = strtoull(requested, &end, 10);
        if (errno != 0 || end == requested || !end || *end != '\0' ||
                parsed == 0 || parsed > (unsigned long long)SIZE_MAX) {
            log_error("gc-force-collect: invalid LAMBDA_GC_FORCE_EVERY=%s; expected a positive integer",
                requested);
        } else {
            gc_set_force_collect_interval(gc, (size_t)parsed);
            log_info("gc-force-collect: configured every %zu public GC allocations for heap=%p",
                (size_t)parsed, (void*)gc);
        }
    }

    const char* seed_text = getenv("LAMBDA_GC_FORCE_SEED");
    const char* odds_text = getenv("LAMBDA_GC_FORCE_ONE_IN");
    if ((!seed_text || !seed_text[0]) && (!odds_text || !odds_text[0])) return;
    if (!seed_text || !seed_text[0] || !odds_text || !odds_text[0]) {
        log_error("gc-force-collect: LAMBDA_GC_FORCE_SEED and LAMBDA_GC_FORCE_ONE_IN must be set together");
        return;
    }
    errno = 0;
    char* seed_end = NULL;
    unsigned long long seed = strtoull(seed_text, &seed_end, 0);
    bool seed_invalid = errno != 0 || seed_end == seed_text ||
        !seed_end || *seed_end != '\0';
    errno = 0;
    char* odds_end = NULL;
    unsigned long odds = strtoul(odds_text, &odds_end, 10);
    bool odds_invalid = errno != 0 || odds_end == odds_text || !odds_end ||
        *odds_end != '\0' || odds == 0 || odds > UINT32_MAX;
    if (seed_invalid || odds_invalid) {
        log_error("gc-force-collect: invalid random schedule seed=%s one_in=%s",
            seed_text, odds_text);
        return;
    }
    gc_set_force_collect_random(gc, (uint64_t)seed, (uint32_t)odds);
    log_info("gc-force-collect: configured deterministic random seed=%llu one_in=%u for heap=%p",
        seed, (unsigned)odds, (void*)gc);
}

static void heap_configure_gc_poisoning(gc_heap_t* gc) {
    if (!gc) return;
    const char* requested = getenv("LAMBDA_GC_POISON_FREED");
    if (!requested || !requested[0] || strcmp(requested, "0") == 0) return;
    if (strcmp(requested, "1") != 0) {
        log_error("gc-poison-freed: invalid LAMBDA_GC_POISON_FREED=%s; expected 0 or 1",
            requested);
        return;
    }
    gc_set_poison_freed(gc, 1);
    log_info("gc-poison-freed: enabled byte=0x%02X for heap=%p",
        GC_FREED_POISON_BYTE, (void*)gc);
}

static void init_ascii_char_table() {
    for (int i = 0; i < 128; i++) {
        String* s = (String*)(ascii_char_storage + i * ASCII_CHAR_ENTRY_SIZE);
        s->len = 1;
        s->is_ascii = 1;
        s->chars[0] = (char)i;
        s->chars[1] = '\0';
        ascii_char_table[i] = s;
    }
    ascii_char_table_initialized = true;
}

// get interned single-char ASCII string (returns nullptr if not initialized or ch >= 128)
extern "C" String* get_ascii_char_string(unsigned char ch) {
    if (ch >= 128 || !ascii_char_table_initialized) return nullptr;
    return ascii_char_table[ch];
}

void heap_init() {
    log_debug("heap init: %p", context);
    context->heap = (Heap*)mem_calloc(1, sizeof(Heap), MEM_CAT_EVAL);
    context->heap->gc = mem_gc_heap_create(NULL, MEM_ROLE_RUNTIME_HEAP, "eval.heap");
    context->heap->pool = context->heap->gc->pool;  // alias for compatibility

    // register a stable heap-owned result slot. EvalContext is often stack-local
    // in JS/document paths, so registering context->result would leave stale
    // root-slot addresses after that frame returns.
    context->heap->result_root = context->result.item;
    gc_register_root(context->heap->gc, &context->heap->result_root);

    // set the auto-collection callback so GC triggers when data zone fills up
    gc_set_collect_callback(context->heap->gc, heap_gc_collect);
    heap_configure_gc_force_schedule(context->heap->gc);
    heap_configure_gc_poisoning(context->heap->gc);

    // register VMap tracing/finalization callbacks
    context->heap->gc->vmap_trace = vmap_gc_trace;
    context->heap->gc->vmap_destroy = gc_destroy_vmap_object;
    context->heap->gc->error_trace = err_gc_trace;
    context->heap->gc->error_destroy = err_gc_destroy;
    context->heap->gc->js_native_trace = js_native_map_gc_trace;
    context->heap->gc->js_function_trace = js_function_gc_trace;
    context->heap->gc->js_function_compact = js_function_gc_compact;
    context->heap->gc->external_destroy = gc_destroy_external_payload;
    err_set_heap_allocator(heap_calloc);

    // initialize interned single-char ASCII table (one-time, idempotent)
    if (!ascii_char_table_initialized) {
        init_ascii_char_table();
    }
}

void heap_init_with_pool(Pool* pool) {
    log_debug("heap init with pool: %p (pool=%p)", context, pool);
    context->heap = (Heap*)mem_calloc(1, sizeof(Heap), MEM_CAT_EVAL);
    context->heap->gc = mem_gc_heap_create_with_pool(NULL, pool, MEM_ROLE_RUNTIME_HEAP, "eval.heap");
    context->heap->pool = context->heap->gc->pool;

    context->heap->result_root = context->result.item;
    gc_register_root(context->heap->gc, &context->heap->result_root);
    gc_set_collect_callback(context->heap->gc, heap_gc_collect);
    heap_configure_gc_force_schedule(context->heap->gc);
    heap_configure_gc_poisoning(context->heap->gc);
    context->heap->gc->vmap_trace = vmap_gc_trace;
    context->heap->gc->vmap_destroy = gc_destroy_vmap_object;
    context->heap->gc->error_trace = err_gc_trace;
    context->heap->gc->error_destroy = err_gc_destroy;
    context->heap->gc->js_native_trace = js_native_map_gc_trace;
    context->heap->gc->js_function_trace = js_function_gc_trace;
    context->heap->gc->js_function_compact = js_function_gc_compact;
    context->heap->gc->external_destroy = gc_destroy_external_payload;
    err_set_heap_allocator(heap_calloc);

    if (!ascii_char_table_initialized) {
        init_ascii_char_table();
    }
}

// Trigger a collection from the runtime using only registered and side-stack
// Item roots. Native C/C++ frames are never inspected.
__attribute__((noinline))
extern "C" void heap_gc_collect(void) {
    JS_WEAK_PROPERTY_SET_BRANCH("heap_gc_collect");
    if (!context || !context->heap || !context->heap->gc) return;
    gc_heap_t* gc = context->heap->gc;

    int64_t side_root_count = 0;
    uint64_t* side_root_base = context->side_root_base;
    if (side_root_base && context->side_root_top >= side_root_base) {
        side_root_count = context->side_root_top - side_root_base;
    }
    gc_collect_with_root_region(gc, NULL, 0,
        side_root_base, side_root_count);
    // Side-stack virtual reservations are cheap; return untouched committed
    // pages after a collection so transient recursion does not set the RSS floor.
    lambda_side_stack_decommit_unused(context);
}

// register an external root slot (e.g., BSS global address)
extern "C" void heap_register_gc_root(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_register_root(context->heap->gc, slot);
}

extern "C" bool heap_register_gc_root_for(Context* runtime, uint64_t* slot) {
    EvalContext* owner = (EvalContext*)runtime;
    if (!owner || !owner->heap || !owner->heap->gc || !slot) return false;
    return gc_try_register_root(owner->heap->gc, slot) != 0;
}

extern "C" void heap_unregister_gc_root_for(Context* runtime, uint64_t* slot) {
    EvalContext* owner = (EvalContext*)runtime;
    if (!owner || !owner->heap || !owner->heap->gc || !slot) return;
    gc_unregister_root(owner->heap->gc, slot);
}

extern "C" void heap_no_gc_scope_begin(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (owner && owner->heap) gc_no_gc_scope_begin(owner->heap->gc);
}

extern "C" void heap_no_gc_scope_end(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (owner && owner->heap) gc_no_gc_scope_end(owner->heap->gc);
}

extern "C" void heap_gc_defer_collection_begin(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (owner && owner->heap && owner->heap->gc) {
        gc_defer_collection_begin(owner->heap->gc);
    }
}

extern "C" void heap_gc_defer_collection_end(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (owner && owner->heap && owner->heap->gc) {
        gc_defer_collection_end(owner->heap->gc);
    }
}

extern "C" uint64_t* heap_gc_root_slot_new(uint64_t value) {
    uint64_t* slot = (uint64_t*)mem_alloc(sizeof(uint64_t), MEM_CAT_EVAL);
    if (!slot) return NULL;
    *slot = value;
    heap_register_gc_root(slot);
    return slot;
}

// register a contiguous range of Items as GC roots (e.g., JS closure env arrays)
extern "C" void heap_register_gc_root_range(uint64_t* base, int count) {
    if (!context || !context->heap || !context->heap->gc || !base || count <= 0) return;
    gc_register_root_range(context->heap->gc, base, count);
}

// unregister a previously-registered root range by base pointer
extern "C" void heap_unregister_gc_root_range(uint64_t* base) {
    if (!context || !context->heap || !context->heap->gc || !base) return;
    gc_unregister_root_range(context->heap->gc, base);
}

// unregister an external root slot
extern "C" void heap_unregister_gc_root(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_unregister_root(context->heap->gc, slot);
}

extern "C" void heap_register_gc_weak(uint64_t* slot,
        gc_weak_clear_fn on_clear, void* weak_context) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_register_weak(context->heap->gc, slot, on_clear, weak_context);
}

extern "C" void heap_unregister_gc_weak(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_unregister_weak(context->heap->gc, slot);
}

void* heap_alloc(int size, TypeId type_id) {
    if (type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64 ||
            type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64) {
        log_error("heap-scalar-invariant: rejected scalar allocation type=%u",
            (unsigned)type_id);
        abort();
    }
    gc_heap_t *gc = context->heap->gc;
    void *data = gc_heap_alloc(gc, size, type_id);
    if (!data) {
        log_error("failed to allocate memory for heap entry");
        return NULL;
    }
    heap_assert_raw_item_allocation(data, type_id);
    return data;
}

// declared extern "C" to allow calling from C code (path.c)
extern "C" void* heap_calloc(size_t size, TypeId type_id) {
    if (type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64 ||
            type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64) {
        log_error("heap-scalar-invariant: rejected scalar calloc type=%u",
            (unsigned)type_id);
        abort();
    }
    gc_heap_t *gc = context->heap->gc;
    void* ptr = gc_heap_calloc(gc, size, type_id);
    if (!ptr) return NULL;
    // mark containers as heap-owned so free_container can distinguish from arena-owned
    // Note: Function and Type have different byte-1 layout (not Container flags), skip them
    if (type_id >= LMD_TYPE_CONTAINER && type_id != LMD_TYPE_FUNC && type_id != LMD_TYPE_TYPE) {
        ((Container*)ptr)->is_heap = 1;
    }
    heap_assert_raw_item_allocation(ptr, type_id);
    return ptr;
}

extern "C" int64_t lambda_restore_number_frame_top(Context* runtime,
        uint64_t* top) {
    if (!runtime || !top || !runtime->side_number_base ||
            !runtime->side_number_top || top < runtime->side_number_base ||
            top > runtime->side_number_top) {
        // This NO_GC JIT epilogue cannot enter logging because formatting may
        // allocate before the invalid scalar-frame invariant is terminated.
        abort();
    }
#ifndef NDEBUG
    // A returned Item must have been adopted before this watermark is restored;
    // poison makes any missed activation-home escape fail deterministically.
    memset(top, 0xA5, (size_t)(runtime->side_number_top - top) * sizeof(uint64_t));
#endif
    runtime->side_number_top = top;
    return 0;
}

extern "C" void* heap_calloc_closure_env(size_t size) {
    if (!context || !context->heap || !context->heap->gc || size == 0) return NULL;
    if (size > SIZE_MAX / 2) return NULL;
    // Closure slots own a parallel scalar tail so captured wide numbers never
    // retain pointers into a completed invocation's number frame.
    return gc_heap_calloc(context->heap->gc, size * 2, GC_TYPE_JS_ENV);
}

// Specialized allocator for JIT: uses bump-pointer fast path with pre-computed
// size class. Falls back to free list recycling when bump is full.
extern "C" void* heap_calloc_class(size_t size, TypeId type_id, int cls) {
    if (!context || !context->heap) {
        log_error("heap_calloc_class: context=%p — called before runtime init", (void*)context);
        return NULL;
    }
    if (type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64 ||
            type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64) {
        log_error("heap-scalar-invariant: rejected scalar class allocation type=%u",
            (unsigned)type_id);
        abort();
    }
    gc_heap_t *gc = context->heap->gc;
    size_t class_size = gc_object_zone_class_size(cls);
    size_t slot_size = sizeof(gc_header_t) + class_size;
    void* ptr = gc_heap_bump_alloc(gc, slot_size, size, type_id, cls);
    if (!ptr) return NULL;
    if (type_id >= LMD_TYPE_CONTAINER && type_id != LMD_TYPE_FUNC && type_id != LMD_TYPE_TYPE) {
        ((Container*)ptr)->is_heap = 1;
    }
    heap_assert_raw_item_allocation(ptr, type_id);
    return ptr;
}

// allocate variable-size data (items[], data buffers) from the GC data zone
// these are bump-allocated, not individually freeable — reclaimed at GC
extern "C" void* heap_data_alloc(size_t size) {
    JS_WEAK_PROPERTY_SET_BRANCH("heap_data_alloc");
    if (!context || !context->heap) {
        log_error("heap_data_alloc: context=%p — called before runtime init", (void*)context);
        return NULL;
    }
    gc_heap_t *gc = context->heap->gc;
    if (!gc) {
        log_error("heap_data_alloc: gc=%p heap=%p — gc is null", (void*)gc, (void*)context->heap);
        return NULL;
    }
    void* ptr = gc_data_alloc(gc, size);
    if (!ptr) {
        log_error("heap_data_alloc: failed to allocate %zu bytes from data zone (gc=%p)", size, (void*)gc);
        return NULL;
    }
    return ptr;
}

// allocate zeroed variable-size data from the GC data zone
extern "C" void* heap_data_calloc(size_t size) {
    void* ptr = heap_data_alloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

// create a content string by copying from source (NOT pooled - arena allocated)
// use this for user content, text data, or any non-structural strings
// declared extern "C" to allow calling from C code (path.c)
extern "C" String* heap_strcpy(const char* src, int64_t len) {
    // guard against a negative length and against the size overflowing heap_alloc's int
    // parameter (which would otherwise truncate to a small allocation + large memcpy).
    if (len < 0 || (uint64_t)len + 1 + sizeof(String) > (uint64_t)INT_MAX) return NULL;
    String *str = (String *)heap_alloc((int)(len + 1 + sizeof(String)), LMD_TYPE_STRING);
    if (!str) return NULL;         // OOM — propagate instead of dereferencing NULL
    memcpy(str->chars, src, len);  // Safe copy with explicit length
    str->chars[len] = '\0';        // Explicit null termination
    str->len = len;
    str->is_ascii = str_is_ascii(str->chars, len) ? 1 : 0;
    return str;
}

extern "C" Binary* heap_binary_from_storage(ByteStorage* storage, size_t offset,
        size_t length, bool is_ascii) {
    if (!storage || length > UINT32_MAX || offset > storage->capacity ||
        length > storage->capacity - offset || (length > 0 && !storage->data)) return NULL;
    Binary* binary = (Binary*)heap_alloc((int)sizeof(Binary), LMD_TYPE_BINARY);
    if (!binary) return NULL;
    // A failed retain must leave a finalizer-safe GC descriptor.
    memset(binary, 0, sizeof(Binary));
    if (!binary_init_storage(binary, storage, offset, length, is_ascii)) return NULL;
    return binary;
}

extern "C" Binary* heap_binary_from_bytes(const char* src, int64_t len) {
    if (len < 0 || (len > 0 && !src) || (uint64_t)len > UINT32_MAX) return NULL;
    ByteStorage* storage = byte_storage_alloc((size_t)len, MEM_CAT_CONTAINER);
    if (!storage) return NULL;
    if (len > 0) {
        memcpy(storage->data, src, (size_t)len);
        binary_record_payload_copy();
    }
    bool is_ascii = len == 0 || str_is_ascii(src, (size_t)len);
    Binary* binary = heap_binary_from_storage(storage, 0, (size_t)len, is_ascii);
    byte_storage_release(storage);
    return binary;
}

extern "C" Binary* heap_binary_slice(Binary* source, size_t offset, size_t length) {
    if (!source || offset > source->len || length > source->len - offset) return NULL;
    const uint8_t* data = binary_data(source);
    if (length > 0 && !data) return NULL;
    if (source->storage) {
        Binary* binary = (Binary*)heap_alloc((int)sizeof(Binary), LMD_TYPE_BINARY);
        if (!binary) return NULL;
        memset(binary, 0, sizeof(Binary));
        return binary_init_slice(binary, source, offset, length) ? binary : NULL;
    }
    return heap_binary_from_bytes((const char*)data + offset, (int64_t)length);
}

extern "C" Binary* heap_binary_copy(Binary* source) {
    if (!source) return NULL;
    return heap_binary_from_bytes((const char*)binary_data(source), source->len);
}

extern "C" Binary* heap_binary_concat(Binary* left, Binary* right) {
    if (!left || !right || left->len > UINT32_MAX - right->len) return NULL;
    size_t length = (size_t)left->len + right->len;
    ByteStorage* storage = byte_storage_alloc(length, MEM_CAT_CONTAINER);
    if (!storage) return NULL;
    // Concatenation stays contiguous and length-based because bytes may contain NUL.
    if (left->len > 0) memcpy(storage->data, binary_data(left), left->len);
    if (right->len > 0) memcpy(storage->data + left->len, binary_data(right), right->len);
    if (length > 0) binary_record_payload_copy();
    Binary* binary = heap_binary_from_storage(storage, 0, length,
        left->is_ascii && right->is_ascii);
    byte_storage_release(storage);
    return binary;
}

// create a name string using runtime name_pool (ALWAYS pooled via string interning)
// use this for structural identifiers: map keys, element tags, attribute names, etc.
// same name string will always return the same pointer (enables identity comparison)
// inherits from parent name_pool if available (efficient for schema/document hierarchies)
String* heap_create_name(const char* name, size_t len) {
    if (!context || !context->name_pool) {
        log_error("heap_create_name called with invalid context or name_pool");
        return nullptr;
    }
    return name_pool_create_len(context->name_pool, name, len);
}

String* heap_create_name(const char* name) {
    if (!name) return nullptr;
    return heap_create_name(name, strlen(name));
}

// create a symbol using heap allocation
// Symbol is a separate struct from String with an ns (namespace) field
Symbol* heap_create_symbol(const char* symbol, size_t len) {
    if (!context || !symbol) {
        log_error("heap_create_symbol called with invalid context or null symbol");
        return nullptr;
    }
    if (len == 0) {
        // Symbols are solid identifiers; runtime producers return null for zero-length results.
        return nullptr;
    }
    Symbol* sym = (Symbol*)heap_alloc(sizeof(Symbol) + len + 1, LMD_TYPE_SYMBOL);
    sym->len = len;
    sym->ns = nullptr;
    memcpy(sym->chars, symbol, len);
    sym->chars[len] = '\0';
    return sym;
}

Symbol* heap_create_symbol(const char* symbol) {
    if (!symbol) return nullptr;
    return heap_create_symbol(symbol, strlen(symbol));
}

static Item box_float_number_stack(double dval) {
    if (!context || (!context->side_number_top && !lambda_side_stack_bind(context))) {
        log_error("number-stack float boxing called with invalid context");
        return ItemError;
    }
    double *dptr = (double*)lambda_side_number_alloc(context);
    if (!dptr) {
        lambda_stack_overflow_error("number-side-stack");
        return ItemError;
    }
    *dptr = dval;
    return {.item = d2it(dptr)};
}

Item flt2it(double dval) {
    uint64_t bits;
    memcpy(&bits, &dval, sizeof(bits));
    if (dval == 0.0) {
        return {.item = ITEM_FLOAT_P0 | (signbit(dval) ? UINT64_C(1) : UINT64_C(0))};
    }
    if (bits & ITEM_DBL_MASK) {
        return {.item = bits};
    }
    // S1 canonical encoding: tiny/subnormal out-of-band doubles stay boxed.
    return box_float_number_stack(dval);
}

Item push_d(double dval) {
    return flt2it(dval);
}

extern "C" uint64_t lambda_mir_double_bits(double dval) {
    AutoAssertNoGC no_gc((Context*)context);
    uint64_t bits;
    memcpy(&bits, &dval, sizeof(bits));
    return bits;
}

extern "C" double lambda_mir_bits_double(uint64_t bits) {
    AutoAssertNoGC no_gc((Context*)context);
    double dval;
    memcpy(&dval, &bits, sizeof(dval));
    return dval;
}

extern "C" Item lambda_item_adopt_scalar_home(Item item, uint64_t* home) {
    AutoAssertNoGC no_gc((Context*)context);
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        break;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        if ((item.item & ITEM_DBL_MASK) || item.item == ITEM_FLOAT_P0 ||
                item.item == ITEM_FLOAT_N0) return item;
        break;
    default:
        return item;
    }
    // Copy before watermark restore to prevent unbounded frame donation.
    if (!home) {
        // A missing ABI home would return a dead activation pointer.
        abort();
    }
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64:
        *(int64_t*)home = item.get_int64();
        return {.item = l2it((int64_t*)home)};
    case LMD_TYPE_UINT64:
        *(uint64_t*)home = item.get_uint64();
        return {.item = u2it((uint64_t*)home)};
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        double value = item.get_double();
        memcpy(home, &value, sizeof(value));
        return lambda_float_ptr_to_item((double*)home);
    }
    default:
        return item;
    }
}

Item box_int64_value(int64_t lval) {
    if (!context || (!context->side_number_top && !lambda_side_stack_bind(context))) {
        log_error("int64 number-home boxing called with invalid context");
        return ItemError;
    }
    // INT64 never uses an inline Item so every transient value follows the
    // same return and ownership protocol regardless of magnitude.
    int64_t *lptr = (int64_t*)lambda_side_number_alloc(context);
    if (!lptr) {
        lambda_stack_overflow_error("number-side-stack");
        return ItemError;
    }
    *lptr = lval;
    return {.item = l2it(lptr)};
}

Item box_uint64_value(uint64_t uval) {
    if (!context || (!context->side_number_top && !lambda_side_stack_bind(context))) {
        log_error("uint64 number-home boxing called with invalid context");
        return ItemError;
    }
    // UINT64 shares the one-word scalar-home protocol with INT64; transient
    // unsigned values must not allocate a standalone GC payload.
    uint64_t* uptr = lambda_side_number_alloc(context);
    if (!uptr) {
        lambda_stack_overflow_error("number-side-stack");
        return ItemError;
    }
    *uptr = uval;
    return {.item = u2it(uptr)};
}

// Native helpers that return raw int64_t retain this historical error channel.
// Ordinary language values must call box_int64_value(), which preserves INT64_MAX.
Item box_int64_result_or_error(int64_t lval) {
    if (lval == INT64_ERROR) return ItemError;
    return box_int64_value(lval);
}

// Safe version of push_d that detects already-boxed FLOAT Items.
// When the MIR JIT passes a value that's already a boxed FLOAT Item
// (from a runtime function return), this prevents double-boxing.
Item push_d_safe(double val) {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    if (bits & ITEM_DBL_MASK) {
        // Already an inline FLOAT Item — return as-is.
        return {.item = bits};
    }
    uint8_t tag = bits >> 56;

    if (tag == LMD_TYPE_FLOAT) {
        // Already a boxed FLOAT Item — return as-is
        Item result;
        result.item = bits;
        return result;
    }
    // Raw double value — box normally
    return push_d(val);
}

extern "C" void heap_finalize_gc_objects(gc_heap_t *gc) {
    if (!gc) return;
    gc_finalize_all_objects(gc);
}

void heap_destroy() {
    if (context->heap) {
        // Runtime item cleanup may neuter owning Array objects, so it must run
        // while the GC heap pool that contains those owners is still alive.
        js_array_runtime_items_cleanup_all();
        if (context->heap->gc) {
            // finalize all GC-managed objects: free sub-allocations (items[], data, mpd_t, closure_env)
            // that were malloc'd/calloc'd separately from the pool
            heap_finalize_gc_objects(context->heap->gc);
            gc_heap_destroy(context->heap->gc);  // pool_destroy frees all pool memory
        }
        context->heap->pool = NULL;
        mem_free(context->heap);
    }
}

void mir_guest_finish_context(Runtime* runtime, EvalContext* old_context,
                              bool reusing_context) {
    if (!reusing_context && context) {
        if (runtime) {
            // A standalone guest result may reference this heap after the MIR
            // entrypoint returns. Transfer all context-owned state so the one
            // runtime cleanup path can release it after output is consumed.
            runtime->heap = context->heap;
            runtime->name_pool = context->name_pool;
            runtime->type_list = (ArrayList*)context->type_list;
        } else {
            if (context->name_pool) name_pool_release(context->name_pool);
            if (context->type_list) arraylist_free((ArrayList*)context->type_list);
            if (context->heap) heap_destroy();
        }
    }
    context = old_context;
    _lambda_rt = (Context*)old_context;
}

// finalize all GC-managed objects before pool_destroy.
// walks all_objects and frees external sub-allocations (not zone-managed).
// items[], data buffers, closure_env are in the data zone — no free needed.
// Only external resources (mpd_t, VMap backing HashMap) need explicit cleanup.
static void gc_finalize_all_objects(gc_heap_t *gc) {
    gc_native_seen_t seen_native;
    gc_native_seen_init(&seen_native);
    gc_header_t *current = gc->all_objects;
    while (current) {
        if (current->gc_flags & GC_FLAG_FREED) {
            current = current->next;
            continue;
        }
        void *obj = (void*)(current + 1);
        uint16_t tag = current->type_tag;

        if (tag == LMD_TYPE_VMAP) {
            VMap *vm = (VMap*)obj;
            // Host payload ownership is independent of the optional lazy map backing.
            gc_finalize_vmap_host_payload(vm);
            if (vm->vtable && vm->data) {
                vm->vtable->destroy(vm->data);
                vm->data = NULL;
            }
        }
        else if (tag == LMD_TYPE_DECIMAL) {
            Decimal *dec = (Decimal*)obj;
            if (dec->dec_val) {
                mpd_del(dec->dec_val);
                dec->dec_val = NULL;
            }
        }
        else if (tag == LMD_TYPE_ERROR) {
            err_gc_destroy(obj);
        }
        else if (tag == LMD_TYPE_ARRAY) {
            Array *arr = (Array*)obj;
            if (arr->items && js_array_runtime_items_release(arr->items)) {
                arr->items = NULL;
                arr->capacity = 0;
            }
        }
        else if (tag == LMD_TYPE_MAP) {
            gc_finalize_js_native_map((Map*)obj, &seen_native);
        }
        // All other types: items[], data, closure_env are zone-managed (data zone or object zone)
        // and will be bulk-freed by zone/pool destruction. No individual free needed.
        current = current->next;
    }
    gc_native_seen_dispose(&seen_native);
}

void print_heap_entries() {
#ifndef NDEBUG
    gc_heap_t *gc = context->heap->gc;
    log_debug("after exec gc objects: %zu", gc->object_count);
    gc_header_t *header = gc->all_objects;
    int idx = 0;
    while (header) {
        if (header->gc_flags & GC_FLAG_FREED) { header = header->next; idx++; continue; }
        void *data = (void*)(header + 1);
        uint16_t type_tag = header->type_tag;
        log_debug("gc object index: %d, type: %s, data: %p", idx, type_info[type_tag].name, data);
        header = header->next;
        idx++;
    }
#endif
}

void check_memory_leak() {
#ifndef NDEBUG
    gc_heap_t *gc = context->heap->gc;
    log_debug("gc objects at shutdown: %zu (all freed by pool_destroy)", gc->object_count);
#endif
}

// free_item / free_container / free_map_item are no longer called.
// All memory is freed at context end via gc_finalize_all_objects + pool_destroy.
// These stubs remain for API compatibility with any external callers.
void free_item(Item item, bool clear_entry) {
    (void)item; (void)clear_entry;
}

void free_container(Container* cont, bool clear_entry) {
    (void)cont; (void)clear_entry;
}

void frame_start() {
    // no-op: frame management removed (GC Phase 4)
}

void frame_end() {
    // no-op: frame management removed (GC Phase 4)
}
