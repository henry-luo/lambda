#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/gc_heap.h"

extern __thread EvalContext* context;

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
    str->len = len;  str->ref_cnt = 0;
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
    sym->ref_cnt = 1;
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
        if (context->heap->gc) gc_heap_destroy(context->heap->gc);
        context->heap->pool = NULL;  // pool was owned by gc_heap, now destroyed
        free(context->heap);
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
        if (type_tag >= LMD_TYPE_CONTAINER && type_tag <= LMD_TYPE_OBJECT) {
            Container *cont = (Container*)data;
            log_debug("gc object container: type: %s, ref_cnt: %d",
                type_info[type_tag].name, cont->ref_cnt);
        }
        header = header->next;
        idx++;
    }
}

void check_memory_leak() {
    StrBuf *strbuf = strbuf_new_cap(1024);
    gc_heap_t *gc = context->heap->gc;
    log_debug("check gc objects: %zu", gc->object_count);
    gc_header_t *header = gc->all_objects;
    int idx = 0;
    while (header) {
        if (header->gc_flags & GC_FLAG_FREED) { header = header->next; idx++; continue; }
        void* data = (void*)(header + 1);
        uint16_t type_tag = header->type_tag;
        log_debug("gc object index: %d, type: %s, data: %p", idx, type_info[type_tag].name, data);
        if (type_tag == LMD_TYPE_LIST) {
            List *list = (List*)data;
            log_debug("gc object list: %p, length: %ld, ref_cnt: %d", list, list->length, list->ref_cnt);
            Item itm = {.list = list};
            strbuf_reset(strbuf);
            print_item(strbuf, itm);
            log_debug("gc object list: %s", strbuf->str);
        }
        else if (type_tag == LMD_TYPE_ARRAY) {
            Array *arr = (Array*)data;
            log_debug("gc object array: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
        }
        else if (type_tag == LMD_TYPE_ARRAY_INT) {
            ArrayInt *arr = (ArrayInt*)data;
            log_debug("gc object array int: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
        }
        else if (type_tag == LMD_TYPE_ARRAY_INT64) {
            ArrayInt64 *arr = (ArrayInt64*)data;
            log_debug("gc object array int64: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
        }
        else if (type_tag == LMD_TYPE_ARRAY_FLOAT) {
            ArrayFloat *arr = (ArrayFloat*)data;
            log_debug("gc object array float: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
        }
        else if (type_tag == LMD_TYPE_MAP || type_tag == LMD_TYPE_ELEMENT || type_tag == LMD_TYPE_OBJECT) {
            Map *map = (Map*)data;
            log_debug("gc object map/object: %p, length: %ld, ref_cnt: %d", map,
                ((TypeMap*)map->type)->length, map->ref_cnt);
        }
        header = header->next;
        idx++;
    }
}

void free_container(Container* cont, bool clear_entry);

void free_map_item(ShapeEntry *field, void* map_data, bool clear_entry) {
    log_debug("free_map_item: field=%p, map_data=%p, clear_entry=%d", field, map_data, clear_entry);
    while (field) {
        const char* field_name = field->name ? "named_field" : "(nested)";
        log_debug("freeing map field: name=%s, name_ptr=%p, type=%d, byte_offset=%d",
                  field_name, field->name,
                  field->type ? field->type->type_id : -1, field->byte_offset);
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        log_debug("field_ptr=%p", field_ptr);
        if (!field->name) { // nested map
            Map *nested_map = *(Map**)field_ptr;
            log_debug("nested_map pointer: %p", nested_map);
            if (nested_map && nested_map->is_heap) {
                log_debug("nested_map: data=%p, ref_cnt=%d, type=%p",
                          nested_map->data, nested_map->ref_cnt, nested_map->type);
                // delink the nested map
                if (nested_map->ref_cnt > 0) nested_map->ref_cnt--;
                if (!nested_map->ref_cnt) free_container((Container*)nested_map, clear_entry);
            }
        }
        else if (field->type->type_id == LMD_TYPE_STRING || field->type->type_id == LMD_TYPE_SYMBOL ||
            field->type->type_id == LMD_TYPE_BINARY) {
            String *str = *(String**)field_ptr;
            if (str) {
                Item item = {.item = s2it(str)};
                free_item(item, clear_entry);
            }
        }
        else if (field->type->type_id == LMD_TYPE_DECIMAL) {
            Decimal *dec = *(Decimal**)field_ptr;
            if (dec) {
                Item item = {.item = c2it(dec)};
                free_item(item, clear_entry);
            }
        }
        else if (LMD_TYPE_LIST <= field->type->type_id && field->type->type_id <= LMD_TYPE_OBJECT) {
            Container *container = *(Container**)field_ptr;
            if (container && container->is_heap) {
                // only manage heap-owned containers
                if (container->ref_cnt > 0) container->ref_cnt--;
                if (!container->ref_cnt) free_container(container, clear_entry);
            }
        }
        else if (field->type->type_id == LMD_TYPE_FUNC) {
            Function *fn = *(Function**)field_ptr;
            if (fn) {
                if (fn->ref_cnt > 0) fn->ref_cnt--;
                if (!fn->ref_cnt) {
                    log_debug("freeing map field function: %p", fn);
                    gc_heap_pool_free(context->heap->gc, fn);
                }
            }
        }
        field = field->next;
    }
}

void free_container(Container* cont, bool clear_entry) {
    if (!cont) return;
    // only free heap-owned containers; arena-owned ones are managed by arena lifecycle
    if (!cont->is_heap) return;
    // guard against double-free: check GC_FLAG_FREED
    gc_header_t *hdr = gc_get_header(cont);
    if (hdr->gc_flags & GC_FLAG_FREED) return;
    log_debug("free_container: cont=%p, clear_entry=%d", cont, clear_entry);
    log_debug("container details: type_id=%d, ref_cnt=%d", cont->type_id, cont->ref_cnt);
    assert(cont->ref_cnt == 0);
    gc_heap_t *gc = context->heap->gc;
    TypeId type_id = cont->type_id;
    if (type_id == LMD_TYPE_LIST) {
        List *list = (List *)cont;
        if (!list->ref_cnt) {
            log_debug("freeing list items: %p, length: %ld", list, list->length);
            for (int j = 0; j < list->length; j++) {
                free_item(list->items[j], clear_entry);
            }
            if (list->items && list->items != (Item*)(list + 1)) free(list->items);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY) {
        Array *arr = (Array*)cont;
        log_debug("freeing array: %p, ref_cnt=%d, length=%ld, items=%p", arr, arr->ref_cnt, arr->length, arr->items);
        if (!arr->ref_cnt) {
            log_debug("freeing array items, length=%ld", arr->length);
            for (int j = 0; j < arr->length; j++) {
                log_debug("freeing array item[%d]: type=%d, pointer=%p", j, arr->items[j].type_id(), arr->items[j].item);
                free_item(arr->items[j], clear_entry);
            }
            if (arr->items && arr->items != (Item*)(arr + 1)) {
                log_debug("freeing arr->items array: %p", arr->items);
                free(arr->items);
            }
            log_debug("calling gc_heap_pool_free on array container: %p", cont);
            if ((uint64_t)cont < 0x100000000ULL) {
                log_error("DIAGNOSTIC: Array pointer %p appears to be in low memory!", cont);
                return;
            }
            gc_heap_pool_free(gc, cont);
            log_debug("gc_heap_pool_free completed for array");
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt *arr = (ArrayInt*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64 *arr = (ArrayInt64*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat *arr = (ArrayFloat*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_MAP) {
        Map *map = (Map*)cont;
        log_debug("freeing map: %p, ref_cnt=%d, type=%p, data=%p", map, map->ref_cnt, map->type, map->data);
        if (!map->ref_cnt) {
            ShapeEntry *field = ((TypeMap*)map->type)->shape;
            log_debug("map shape field: %p", field);
            if (field) {
                log_debug("calling free_map_item with field=%p, map->data=%p", field, map->data);
                free_map_item(field, map->data, clear_entry);
            }
            if (map->data) {
                log_debug("freeing map->data: %p", map->data);
                free(map->data);
            }
            log_debug("calling gc_heap_pool_free on map: %p", cont);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_VMAP) {
        VMap *vm = (VMap*)cont;
        if (!vm->ref_cnt) {
            log_debug("freeing vmap: %p, vtable=%p, data=%p", vm, vm->vtable, vm->data);
            if (vm->vtable && vm->data) {
                vm->vtable->destroy(vm->data);
            }
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_ELEMENT) {
        Element *elmt = (Element*)cont;
        if (!elmt->ref_cnt) {
            ShapeEntry *field = ((TypeElmt*)elmt->type)->shape;
            if (field) { free_map_item(field, elmt->data, clear_entry); }
            if (elmt->data) free(elmt->data);
            for (int64_t j = 0; j < elmt->length; j++) {
                free_item(elmt->items[j], clear_entry);
            }
            if (elmt->items) free(elmt->items);
            gc_heap_pool_free(gc, cont);
        }
    }
    else if (type_id == LMD_TYPE_OBJECT) {
        Object *obj = (Object*)cont;
        log_debug("freeing object: %p, ref_cnt=%d, type=%p, data=%p", obj, obj->ref_cnt, obj->type, obj->data);
        if (!obj->ref_cnt) {
            ShapeEntry *field = ((TypeObject*)obj->type)->shape;
            if (field) {
                free_map_item(field, obj->data, clear_entry);
            }
            if (obj->data) free(obj->data);
            gc_heap_pool_free(gc, cont);
        }
    }
}

void free_item(Item item, bool clear_entry) {
    gc_heap_t *gc = context->heap->gc;
    if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL || item._type_id == LMD_TYPE_BINARY) {
        String *str = item.get_string();
        if (str && !str->ref_cnt) {
            gc_heap_pool_free(gc, str);
        }
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        Decimal *dec = item.get_decimal();
        if (dec && !dec->ref_cnt) {
            gc_heap_pool_free(gc, dec);
        }
    }
    else if (item._type_id == LMD_TYPE_FUNC) {
        Function *fn = item.function;
        if (fn) {
            if (fn->ref_cnt > 0) fn->ref_cnt--;
            if (!fn->ref_cnt) {
                log_debug("freeing function item: %p", fn);
                gc_heap_pool_free(gc, fn);
            }
        }
    }
    else if (item._type_id == LMD_TYPE_RAW_POINTER) {
        Container* container = item.container;
        if (container && container->is_heap) {
            // only manage heap-owned containers; arena-owned are managed by arena lifecycle
            if (container->ref_cnt > 0) container->ref_cnt--;
            if (!container->ref_cnt) free_container(container, clear_entry);
        }
    }
    // clear_entry is now a no-op — GCHeader list doesn't need manual entry removal.
    // Objects freed here have their GC_FLAG_FREED set by gc_heap_pool_free.
}

void frame_start() {
    log_debug("entering frame_start");
    gc_heap_frame_push(context->heap->gc);
}

void frame_end() {
    gc_heap_t *gc = context->heap->gc;
    gc_header_t *marker = gc_heap_frame_pop(gc);
    log_debug("entering frame_end, marker=%p", marker);

    // walk from newest allocation back to the frame marker
    gc_header_t *current = gc->all_objects;
    gc_header_t *prev = NULL;

    while (current != marker) {
        gc_header_t *next = current->next;  // save before potential free

        // skip objects already freed by recursive free_container
        if (current->gc_flags & GC_FLAG_FREED) {
            // unlink freed object from list, then actually pool_free
            if (prev) prev->next = next;
            else gc->all_objects = next;
            pool_free(gc->pool, current);  // safe: next was saved above
            current = next;
            continue;
        }

        void *obj = (void*)(current + 1);
        uint16_t type_tag = current->type_tag;
        bool should_free = false;

        if (type_tag == LMD_TYPE_STRING || type_tag == LMD_TYPE_SYMBOL || type_tag == LMD_TYPE_BINARY) {
            String *str = (String*)obj;
            if (!str->ref_cnt) {
                log_debug("freeing gc string: %s", str->chars);
                should_free = true;
            }
        }
        else if (type_tag == LMD_TYPE_DECIMAL) {
            Decimal *dec = (Decimal*)obj;
            if (!dec->ref_cnt) {
                log_debug("freeing gc decimal");
                should_free = true;
            }
        }
        else if (type_tag == LMD_TYPE_FUNC) {
            Function *fn = (Function*)obj;
            if (fn->ref_cnt > 0) {
                // still referenced, keep in list
            } else {
                log_debug("freeing gc function: %p", fn);
                should_free = true;
            }
        }
        else if (type_tag >= LMD_TYPE_CONTAINER) {
            Container *cont = (Container*)obj;
            if (cont->ref_cnt > 0) {
                // adopted, keep in list
            } else {
                // free container and its children recursively
                free_container(cont, false);
                // free_container called gc_heap_pool_free on cont,
                // which marked GC_FLAG_FREED (but deferred pool_free).
                // Unlink from list and actually pool_free now.
                if (prev) prev->next = next;
                else gc->all_objects = next;
                pool_free(gc->pool, current);  // safe: next was saved above
                current = next;
                continue;
            }
        }

        if (should_free) {
            // unlink from list and free
            if (prev) prev->next = next;
            else gc->all_objects = next;
            pool_free(gc->pool, current);  // safe: next was saved above
        } else {
            prev = current;
        }

        current = next;
    }
    log_debug("frame_end complete");
}
