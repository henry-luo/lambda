#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/gc_heap.h"
#include "lambda-decimal.hpp"
#include <mpdecimal.h>

extern __thread EvalContext* context;

static void gc_finalize_all_objects(gc_heap_t *gc);

void heap_init() {
    log_debug("heap init: %p", context);
    context->heap = (Heap*)calloc(1, sizeof(Heap));
    context->heap->gc = gc_heap_create();
    context->heap->pool = context->heap->gc->pool;  // alias for compatibility
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
    // Note: Function has different layout (arity at offset 1 instead of flags), skip it
    if (type_id >= LMD_TYPE_CONTAINER && type_id != LMD_TYPE_FUNC) {
        ((Container*)ptr)->is_heap = 1;
    }
    return ptr;
}

// create a content string by copying from source (NOT pooled - arena allocated)
// use this for user content, text data, or any non-structural strings
// declared extern "C" to allow calling from C code (path.c)
extern "C" String* heap_strcpy(char* src, int len) {
    String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
    memcpy(str->chars, src, len);  // Safe copy with explicit length
    str->chars[len] = '\0';        // Explicit null termination
    str->len = len;
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
    log_debug("push_d: %g", dval);
    if (!context->nursery) {
        log_error("push_d called with invalid context");
        return ItemError;
    }
    double *dptr = gc_nursery_alloc_double(context->nursery, dval);
    return {.item = d2it(dptr)};
}

Item push_l(int64_t lval) {
    log_debug("push_l: %" PRId64, lval);
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
        log_debug("push_l_safe: already boxed INT64");
        Item result;
        result.item = (uint64_t)val;
        return result;
    }
    if (tag == LMD_TYPE_INT) {
        // This is a boxed INT Item — extract the int value and re-box as INT64
        log_debug("push_l_safe: converting boxed INT to INT64");
        Item itm;
        itm.item = (uint64_t)val;
        int64_t real_val = (int64_t)itm.get_int56();
        return push_l(real_val);
    }
    // Raw int64 value — box normally
    return push_l(val);
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
// walks all_objects and frees non-pool sub-allocations for each object.
// does NOT free the objects themselves — pool_destroy handles that.
static void gc_finalize_all_objects(gc_heap_t *gc) {
    gc_header_t *current = gc->all_objects;
    while (current) {
        if (current->gc_flags & GC_FLAG_FREED) {
            current = current->next;
            continue;
        }
        void *obj = (void*)(current + 1);
        uint16_t tag = current->type_tag;

        if (tag == LMD_TYPE_LIST || tag == LMD_TYPE_ARRAY) {
            List *list = (List*)obj;
            if (list->items && list->items != (Item*)(list + 1)) {
                free(list->items);
                list->items = NULL;
            }
        }
        else if (tag == LMD_TYPE_ARRAY_INT) {
            ArrayInt *arr = (ArrayInt*)obj;
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) {
                free(arr->items);
                arr->items = NULL;
            }
        }
        else if (tag == LMD_TYPE_ARRAY_INT64) {
            ArrayInt64 *arr = (ArrayInt64*)obj;
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) {
                free(arr->items);
                arr->items = NULL;
            }
        }
        else if (tag == LMD_TYPE_ARRAY_FLOAT) {
            ArrayFloat *arr = (ArrayFloat*)obj;
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) {
                free(arr->items);
                arr->items = NULL;
            }
        }
        else if (tag == LMD_TYPE_MAP) {
            Map *map = (Map*)obj;
            if (map->data) { free(map->data); map->data = NULL; }
        }
        else if (tag == LMD_TYPE_ELEMENT) {
            Element *elmt = (Element*)obj;
            if (elmt->data) { free(elmt->data); elmt->data = NULL; }
            if (elmt->items && elmt->items != (Item*)(elmt + 1)) {
                free(elmt->items);
                elmt->items = NULL;
            }
        }
        else if (tag == LMD_TYPE_OBJECT) {
            Object *obj_s = (Object*)obj;
            if (obj_s->data) { free(obj_s->data); obj_s->data = NULL; }
        }
        else if (tag == LMD_TYPE_VMAP) {
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
        else if (tag == LMD_TYPE_FUNC) {
            Function *fn = (Function*)obj;
            if (fn->closure_env) {
                free(fn->closure_env);
                fn->closure_env = NULL;
            }
        }
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
