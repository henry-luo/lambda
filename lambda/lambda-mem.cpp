#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/gc/gc_heap.h"
#include "lambda-decimal.hpp"
#include "lambda-stack.h"
#include <mpdecimal.h>
#include <setjmp.h>

extern __thread EvalContext* context;

static void gc_finalize_all_objects(gc_heap_t *gc);

// VMap GC bridge functions (defined in vmap.cpp)
extern "C" void vmap_gc_trace(void* data, gc_heap_t* gc);
extern "C" void vmap_gc_destroy(void* data);

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
    context->heap = (Heap*)calloc(1, sizeof(Heap));
    context->heap->gc = gc_heap_create();
    context->heap->pool = context->heap->gc->pool;  // alias for compatibility

    // register context->result as a GC root slot
    gc_register_root(context->heap->gc, &context->result.item);

    // set the auto-collection callback so GC triggers when data zone fills up
    gc_set_collect_callback(context->heap->gc, heap_gc_collect);

    // register VMap tracing/finalization callbacks
    context->heap->gc->vmap_trace = vmap_gc_trace;
    context->heap->gc->vmap_destroy = vmap_gc_destroy;

    // initialize interned single-char ASCII table (one-time, idempotent)
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

    gc_collect(gc, NULL, 0, stack_base, stack_current);
}

// register an external root slot (e.g., BSS global address)
extern "C" void heap_register_gc_root(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_register_root(context->heap->gc, slot);
}

// register a contiguous range of Items as GC roots (e.g., JS closure env arrays)
extern "C" void heap_register_gc_root_range(uint64_t* base, int count) {
    if (!context || !context->heap || !context->heap->gc || !base || count <= 0) return;
    gc_register_root_range(context->heap->gc, base, count);
}

// unregister an external root slot
extern "C" void heap_unregister_gc_root(uint64_t* slot) {
    if (!context || !context->heap || !context->heap->gc || !slot) return;
    gc_unregister_root(context->heap->gc, slot);
}

void* heap_alloc(int size, TypeId type_id) {
    gc_heap_t *gc = context->heap->gc;
    void *data = gc_heap_alloc(gc, size, type_id);
    if (!data) {
        log_error("failed to allocate memory for heap entry");
        return NULL;
    }
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
    return ptr;
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
    return ptr;
}

// allocate variable-size data (items[], data buffers) from the GC data zone
// these are bump-allocated, not individually freeable — reclaimed at GC
extern "C" void* heap_data_alloc(size_t size) {
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
extern "C" String* heap_strcpy(char* src, int64_t len) {
    String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
    memcpy(str->chars, src, len);  // Safe copy with explicit length
    str->chars[len] = '\0';        // Explicit null termination
    str->len = len;
    str->is_ascii = str_is_ascii(str->chars, len) ? 1 : 0;
    return str;
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

Item push_d(double dval) {
    if (!context->nursery) {
        log_error("push_d called with invalid context");
        return ItemError;
    }
    double *dptr = gc_nursery_alloc_double(context->nursery, dval);
    return {.item = d2it(dptr)};
}

Item push_l(int64_t lval) {
    if (!context->nursery) {
        log_error("push_l called with invalid context");
        return ItemError;
    }
    if (lval == INT64_ERROR) return ItemError;
    int64_t *lptr = gc_nursery_alloc_long(context->nursery, lval);
    return {.item = l2it(lptr)};
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
    if (!context->nursery) {
        log_error("push_k called with invalid context");
        return ItemError;
    }
    DateTime *dtptr = gc_nursery_alloc_datetime(context->nursery, val);
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

void heap_destroy() {
    if (context->heap) {
        if (context->heap->gc) {
            // finalize all GC-managed objects: free sub-allocations (items[], data, mpd_t, closure_env)
            // that were malloc'd/calloc'd separately from the pool
            gc_finalize_all_objects(context->heap->gc);
            gc_heap_destroy(context->heap->gc);  // pool_destroy frees all pool memory
        }
        context->heap->pool = NULL;
        free(context->heap);
    }
}

// finalize all GC-managed objects before pool_destroy.
// walks all_objects and frees external sub-allocations (not zone-managed).
// items[], data buffers, closure_env are in the data zone — no free needed.
// Only external resources (mpd_t, VMap backing HashMap) need explicit cleanup.
static void gc_finalize_all_objects(gc_heap_t *gc) {
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
        // All other types: items[], data, closure_env are zone-managed (data zone or object zone)
        // and will be bulk-freed by zone/pool destruction. No individual free needed.
        current = current->next;
    }
}

void print_heap_entries() {
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
}

void check_memory_leak() {
    gc_heap_t *gc = context->heap->gc;
    log_debug("gc objects at shutdown: %zu (all freed by pool_destroy)", gc->object_count);
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
