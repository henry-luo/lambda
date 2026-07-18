#include "transpiler.hpp"
#include <limits.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"
#include "../lib/gc/gc_heap.h"
#include "mem_factory_rt.h"
#include "lambda-error.h"
#include "jube/jube_registry.h"
#include "js/js_runtime.h"
#include "js/js_exec_profile_weak.h"
#include "js/js_typed_array.h"
#include "lambda-decimal.hpp"
#include "lambda-stack.h"
#include "../lib/side_stack.h"
#include "binary.h"
#include <mpdecimal.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h>

extern __thread EvalContext* context;
extern "C" void js_generator_map_gc_trace(Map* map, gc_heap_t* gc);

static void gc_finalize_all_objects(gc_heap_t *gc);
extern "C" void vmap_gc_destroy(void* obj, void* data);

static void gc_destroy_external_payload(void* obj, uint16_t type_tag) {
    if (!obj) return;
    if (type_tag == LMD_TYPE_BINARY) {
        Binary* binary = (Binary*)obj;
        if (binary->storage) binary_release_storage(binary);
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
Item js_map_get_fast_ext(Map* m, const char* key_str, int key_len, bool* out_found);

// ── Interned single-char ASCII strings (Optimization 4) ──────────────
// Pre-allocated table of 128 String objects for ASCII chars 0-127.
// These are statically allocated (not GC-managed) and never freed.
// Each entry is sizeof(String) + 2 bytes (1 char + null terminator).
#define ASCII_CHAR_ENTRY_SIZE (sizeof(String) + 2)
static char ascii_char_storage[128 * ASCII_CHAR_ENTRY_SIZE];
static String* ascii_char_table[128];
static bool ascii_char_table_initialized = false;

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

    // register VMap tracing/finalization callbacks
    context->heap->gc->vmap_trace = vmap_gc_trace;
    context->heap->gc->vmap_destroy = gc_destroy_vmap_object;
    context->heap->gc->error_trace = err_gc_trace;
    context->heap->gc->error_destroy = err_gc_destroy;
    context->heap->gc->js_native_trace = js_native_map_gc_trace;
    context->heap->gc->js_function_trace = js_function_gc_trace;
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
    context->heap->gc->vmap_trace = vmap_gc_trace;
    context->heap->gc->vmap_destroy = gc_destroy_vmap_object;
    context->heap->gc->error_trace = err_gc_trace;
    context->heap->gc->error_destroy = err_gc_destroy;
    context->heap->gc->js_native_trace = js_native_map_gc_trace;
    context->heap->gc->js_function_trace = js_function_gc_trace;
    context->heap->gc->external_destroy = gc_destroy_external_payload;
    err_set_heap_allocator(heap_calloc);

    if (!ascii_char_table_initialized) {
        init_ascii_char_table();
    }
}

// trigger a GC collection from the runtime.
// gathers roots (registered slots + stack scan) and runs the collector.
// Uses setjmp to flush registers onto the stack so the conservative
// stack scanner can find GC-managed pointers held in CPU registers.
__attribute__((noinline))
extern "C" void heap_gc_collect(void) {
    JS_WEAK_PROPERTY_SET_BRANCH("heap_gc_collect");
    if (!context || !context->heap || !context->heap->gc) return;
    gc_heap_t* gc = context->heap->gc;

    // Use setjmp to flush callee-saved registers onto the stack,
    // ensuring the conservative stack scanner can find GC pointers.
    jmp_buf regs;
    setjmp(regs);

    // get stack bounds for conservative scanning.
    uintptr_t stack_base = _lambda_stack_base;
    uintptr_t stack_current;
#if defined(__aarch64__)
    asm volatile("mov %0, sp" : "=r"(stack_current));
#elif defined(__x86_64__)
    asm volatile("movq %%rsp, %0" : "=r"(stack_current));
#else
    volatile int marker;
    stack_current = (uintptr_t)&marker;
#endif

    log_debug("heap_gc_collect: stack_base=%p stack_current=%p",
              (void*)stack_base, (void*)stack_current);

    int64_t side_root_count = 0;
    uint64_t* side_root_base = context->side_root_base;
    if (side_root_base && context->side_root_top >= side_root_base) {
        side_root_count = context->side_root_top - side_root_base;
    }
    gc_collect_with_root_region(gc, NULL, 0,
        stack_base, stack_current, side_root_base, side_root_count);
    // Side-stack virtual reservations are cheap; return untouched committed
    // pages after a collection so transient recursion does not set the RSS floor.
    lambda_side_stack_decommit_unused(context);
}

// register an external root slot (e.g., BSS global address)
extern "C" void heap_register_gc_root(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_register_root(context->heap->gc, slot);
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

extern "C" void* heap_calloc_js_env(size_t size) {
    if (!context || !context->heap || !context->heap->gc || size == 0) return NULL;
    if (size > SIZE_MAX / 2) return NULL;
    // JS env slots own a parallel scalar tail so captured wide numbers never
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
    uint64_t bits;
    memcpy(&bits, &dval, sizeof(bits));
    return bits;
}

extern "C" double lambda_mir_bits_double(uint64_t bits) {
    double dval;
    memcpy(&dval, &bits, sizeof(dval));
    return dval;
}

extern "C" Item lambda_item_heap_rehome(Item item) {
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64: {
        if (item.is_inline_int64()) return item;
        int64_t* value = (int64_t*)heap_alloc(sizeof(int64_t), LMD_TYPE_INT64);
        if (!value) return ItemError;
        *value = item.get_int64();
        return {.item = l2it(value)};
    }
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        if ((item.item & ITEM_DBL_MASK) || item.item == ITEM_FLOAT_P0 ||
                item.item == ITEM_FLOAT_N0) return item;
        double* value = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!value) return ItemError;
        *value = item.get_double();
        return {.item = d2it(value)};
    }
    case LMD_TYPE_DTIME: {
        DateTime* value = (DateTime*)heap_alloc(sizeof(DateTime), LMD_TYPE_DTIME);
        if (!value) return ItemError;
        *value = item.get_datetime();
        return {.item = k2it(value)};
    }
    default:
        return item;
    }
}

Item box_int64_value(int64_t lval) {
    if (lambda_int64_fits_inline(lval)) {
        return {.item = lambda_inline_int64_to_item_bits(lval)};
    }
    if (!context || (!context->side_number_top && !lambda_side_stack_bind(context))) {
        log_error("push_l called with invalid context");
        return ItemError;
    }
    // Only genuinely wide INT64 values need stable backing storage.
    int64_t *lptr = (int64_t*)lambda_side_number_alloc(context);
    if (!lptr) {
        lambda_stack_overflow_error("number-side-stack");
        return ItemError;
    }
    *lptr = lval;
    return {.item = l2it(lptr)};
}

// Legacy result-channel adapter. New code should use box_int64_value so the
// full int64 domain, including INT64_MAX, remains representable.
Item push_l(int64_t lval) {
    if (lval == INT64_ERROR) return ItemError;
    return box_int64_value(lval);
}

Item box_int64_value_safe(int64_t val) {
    uint8_t tag = (uint64_t)val >> 56;
    if (tag == LMD_TYPE_INT64) return {.item = (uint64_t)val};
    if (tag == LMD_TYPE_INT) {
        Item item = {.item = (uint64_t)val};
        return box_int64_value(item.get_int56());
    }
    return box_int64_value(val);
}

// Safe version of push_l that detects already-boxed INT64 Items.
// When the MIR JIT passes a value that's already a boxed INT64 Item
// (from a runtime function return), this prevents double-boxing.
// Also handles boxed INT Items by extracting and re-boxing as INT64.
Item push_l_safe(int64_t val) {
    uint8_t tag = (uint64_t)val >> 56;
    if (tag == LMD_TYPE_INT64) {
        // Already a boxed INT64 Item — return as-is
        Item result;
        result.item = (uint64_t)val;
        return result;
    }
    if (tag == LMD_TYPE_INT) {
        // This is a boxed INT Item — extract the int value and re-box as INT64
        Item itm;
        itm.item = (uint64_t)val;
        int64_t real_val = (int64_t)itm.get_int56();
        return push_l(real_val);
    }
    // Raw int64 value — box normally
    return push_l(val);
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

Item push_k(DateTime val) {
    // check for DateTime error sentinel before pushing
    if (DATETIME_IS_ERROR(val)) {
        log_debug("push_k: received DateTime error sentinel");
        return ItemError;
    }
    if (!context || (!context->side_number_top && !lambda_side_stack_bind(context))) {
        log_error("push_k called with invalid context");
        return ItemError;
    }
    DateTime *dtptr = (DateTime*)lambda_side_number_alloc(context);
    if (!dtptr) {
        lambda_stack_overflow_error("number-side-stack");
        return ItemError;
    }
    *dtptr = val;
    return {.item = k2it(dtptr)};
}

// Safe version of push_k that detects already-boxed DTIME Items.
// When the MIR JIT passes a value that's already a boxed DTIME Item
// (from a runtime function return), this prevents double-boxing.
Item push_k_safe(DateTime val) {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    uint8_t tag = bits >> 56;

    if (tag == LMD_TYPE_DTIME) {
        // Already a boxed DTIME Item — return as-is
        Item result;
        result.item = bits;
        return result;
    }
    return push_k(val);
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
