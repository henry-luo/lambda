#include "transpiler.hpp"
#include "../lib/log.h"

extern __thread EvalContext* context;

void heap_init() {
    log_debug("heap init: %p", context);
    context->heap = (Heap*)calloc(1, sizeof(Heap));
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    context->heap->pool = pool_create();
    context->heap->entries = arraylist_new(1024);
}

void* heap_alloc(int size, TypeId type_id) {
    Heap *heap = context->heap;
    void *data = pool_alloc(heap->pool, size);
    if (!data) {
        log_error("Failed to allocate memory for heap entry");
        return NULL;
    }
    // scalar pointers needs to be tagged
    void* entry = type_id < LMD_TYPE_CONTAINER ?
        (void*)((((uint64_t)type_id)<<56) | (uint64_t)(data)) : data;
    arraylist_append(heap->entries, entry);
    return data;
}

// declared extern "C" to allow calling from C code (path.c)
extern "C" void* heap_calloc(size_t size, TypeId type_id) {
    void* ptr = heap_alloc(size, type_id);
    memset(ptr, 0, size);
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
    // Safety check: if context is num_stack is NULL
    if (!context->num_stack) {
        log_error("push_d called with invalid context");
        return ItemError;
    }
    double *dptr = num_stack_push_double(context->num_stack, dval);
    return {.item = d2it(dptr)};
}

Item push_l(int64_t lval) {
    log_debug("push_l: %" PRId64, lval);
    // Safety check: if context is num_stack is NULL
    if (!context->num_stack) {
        log_error("push_l called with invalid context");
        return ItemError;
    }
    if (lval == INT64_ERROR) return ItemError;
    int64_t *lptr = num_stack_push_long(context->num_stack, lval);
    return {.item = l2it(lptr)};
}

Item push_k(DateTime val) {
    // Check for DateTime error sentinel before pushing
    if (DATETIME_IS_ERROR(val)) {
        log_debug("push_k: received DateTime error sentinel");
        return ItemError;
    }
    // Safety check: if context is num_stack is NULL
    if (!context->num_stack) {
        log_error("push_k called with invalid context");
        return ItemError;
    }
    DateTime *dtptr = num_stack_push_datetime(context->num_stack, val);
    return {.item = k2it(dtptr)};
}

void heap_destroy() {
    if (context->heap) {
        if (context->heap->pool) pool_destroy(context->heap->pool);
        free(context->heap);
    }
}

Item HEAP_ENTRY_START = {.item = ((uint64_t)LMD_CONTAINER_HEAP_START << 56)};

void print_heap_entries() {
    ArrayList *entries = context->heap->entries;
    log_debug("after exec heap entries: %d", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        Item itm = *(Item*)&data;
        log_debug("heap entry index: %d, type: %d, data: %p", i, itm._type_id, data);
        if (itm._type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)data);
            log_debug("heap entry data: type: %s", type_info[type_id].name);
            if (LMD_TYPE_LIST <= type_id && type_id <= LMD_TYPE_ELEMENT) {
                Container *cont = (Container*)data;
                log_debug("heap entry container: type: %s, ref_cnt: %d",
                    type_info[type_id].name, cont->ref_cnt);
            }
        }
    }
}

void check_memory_leak() {
    StrBuf *strbuf = strbuf_new_cap(1024);
    ArrayList *entries = context->heap->entries;
    log_debug("check heap entries: %d", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void* data = entries->data[i];
        Item itm = *(Item*)&data;
        log_debug("heap entry index: %d, type: %s, data: %p", i, type_info[itm._type_id].name, data);
        if (!data) { continue; }  // skip NULL entries
        if (itm._type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)itm.item);
            log_debug("heap entry data: type: %s", type_info[type_id].name);
            if (type_id == LMD_TYPE_LIST) {
                List *list = (List*)data;
                log_debug("heap entry list: %p, length: %ld, ref_cnt: %d", list, list->length, list->ref_cnt);
                strbuf_reset(strbuf);
                print_item(strbuf, itm);
                log_debug("heap entry list: %s", strbuf->str);
            }
            else if (type_id == LMD_TYPE_ARRAY) {
                Array *arr = (Array*)data;
                log_debug("heap entry array: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_ARRAY_INT) {
                ArrayInt *arr = (ArrayInt*)data;
                log_debug("heap entry array int: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_ARRAY_INT64) {
                ArrayInt64 *arr = (ArrayInt64*)data;
                log_debug("heap entry array int64: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
                ArrayFloat *arr = (ArrayFloat*)data;
                log_debug("heap entry array float: %p, length: %ld, ref_cnt: %d", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
                Map *map = (Map*)data;
                log_debug("heap entry map: %p, length: %ld, ref_cnt: %d", map,
                    ((TypeMap*)map->type)->length, map->ref_cnt);
            }
        }
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
        else if (LMD_TYPE_LIST <= field->type->type_id && field->type->type_id <= LMD_TYPE_ELEMENT) {
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
                    pool_free(context->heap->pool, fn);
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
    log_debug("free_container: cont=%p, clear_entry=%d", cont, clear_entry);
    log_debug("container details: type_id=%d, ref_cnt=%d", cont->type_id, cont->ref_cnt);
    assert(cont->ref_cnt == 0);
    TypeId type_id = cont->type_id;
    if (type_id == LMD_TYPE_LIST) {
        List *list = (List *)cont;
        if (!list->ref_cnt) {
            // free list items
            log_debug("freeing list items: %p, length: %ld", list, list->length);
            for (int j = 0; j < list->length; j++) {
                free_item(list->items[j], clear_entry);
            }
            if (list->items && list->items != (Item*)(list + 1)) free(list->items);
            pool_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY) {
        Array *arr = (Array*)cont;
        log_debug("freeing array: %p, ref_cnt=%d, length=%ld, items=%p", arr, arr->ref_cnt, arr->length, arr->items);
        if (!arr->ref_cnt) {
            // free array items
            log_debug("freeing array items, length=%ld", arr->length);
            for (int j = 0; j < arr->length; j++) {
                log_debug("freeing array item[%d]: type=%d, pointer=%p", j, arr->items[j].type_id(), arr->items[j].item);
                free_item(arr->items[j], clear_entry);
            }
            if (arr->items && arr->items != (Item*)(arr + 1)) {
                log_debug("freeing arr->items array: %p", arr->items);
                free(arr->items);
            }
            log_debug("calling pool_free on array container: %p, pool=%p", cont, context->heap->pool);
            log_debug("checking if pointer is in pool range...");
            // DIAGNOSTIC: Check if this pointer looks valid
            if ((uint64_t)cont < 0x100000000ULL) {
                log_error("DIAGNOSTIC: Array pointer %p appears to be in low memory - likely stack allocated or corrupted!", cont);
                log_error("DIAGNOSTIC: This array was NOT allocated from heap pool and should NOT be freed!");
                // Don't free this - it will crash
                return;
            }
            pool_free(context->heap->pool, cont);
            log_debug("pool_free completed for array");
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt *arr = (ArrayInt*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            pool_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64 *arr = (ArrayInt64*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            pool_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat *arr = (ArrayFloat*)cont;
        if (!arr->ref_cnt) {
            if (arr->items && (void*)arr->items != (void*)(arr + 1)) free(arr->items);
            pool_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_MAP) {
        Map *map = (Map*)cont;
        log_debug("freeing map: %p, ref_cnt=%d, type=%p, data=%p", map, map->ref_cnt, map->type, map->data);
        if (!map->ref_cnt) {
            // free map items based on the shape
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
            log_debug("calling pool_free on map: %p", cont);
            pool_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ELEMENT) {
        Element *elmt = (Element*)cont;
        if (!elmt->ref_cnt) {
            // free element attrs based on the shape
            ShapeEntry *field = ((TypeElmt*)elmt->type)->shape;
            if (field) { free_map_item(field, elmt->data, clear_entry); }
            if (elmt->data) free(elmt->data);
            // free content
            for (int64_t j = 0; j < elmt->length; j++) {
                free_item(elmt->items[j], clear_entry);
            }
            if (elmt->items) free(elmt->items);
            pool_free(context->heap->pool, cont);
        }
    }
}

void free_item(Item item, bool clear_entry) {
    if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL || item._type_id == LMD_TYPE_BINARY) {
        String *str = item.get_string();
        if (str && !str->ref_cnt) {
            pool_free(context->heap->pool, str);
        }
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        Decimal *dec = item.get_decimal();
        if (dec && !dec->ref_cnt) {
            pool_free(context->heap->pool, dec);
        }
    }
    else if (item._type_id == LMD_TYPE_FUNC) {
        Function *fn = item.function;
        if (fn) {
            if (fn->ref_cnt > 0) fn->ref_cnt--;
            if (!fn->ref_cnt) {
                log_debug("freeing function item: %p", fn);
                pool_free(context->heap->pool, fn);
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
    if (clear_entry) {
        ArrayList *entries = context->heap->entries;
        // remove the entry from heap entries
        for (int i = entries->length - 1; i >= 0; i--) {
            void *data = entries->data[i];
            if (data == (void*)item.item) {
                entries->data[i] = NULL;  break;
            }
        }
    }
}

void frame_start() {
    size_t stack_pos = context->num_stack->total_length;
    log_debug("entering frame_start with num stack position: %zu", stack_pos);
    arraylist_append(context->heap->entries, (void*) (((uint64_t)LMD_CONTAINER_HEAP_START << 56) | stack_pos));
    arraylist_append(context->heap->entries, (void*) HEAP_ENTRY_START.item);
}

void frame_end() {
    ArrayList *entries = context->heap->entries;
    log_debug("entering frame_end with entries: %d", entries->length);
    // free heap allocations
    int loop_count = 0;
    int original_length = entries->length;
    for (int i = entries->length - 1; i >= 0; i--) {
        log_debug("frame_end loop: %d, i: %d, original_length: %d", loop_count, i, original_length);
        loop_count++;
        if (loop_count > original_length + 100) {
            log_error("frame_end infinite loop detected! loop_count=%d, i=%d, original_length=%d",
                   loop_count, i, original_length);
            break;
        }
        log_debug("free heap entry index: %d", i);
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        Item itm = {.item = *(uint64_t*)&data};
        if (itm._type_id == LMD_TYPE_STRING || itm._type_id == LMD_TYPE_SYMBOL ||
            itm._type_id == LMD_TYPE_BINARY) {
            String *str = itm.get_string();
            if (str && !str->ref_cnt) {
                log_debug("freeing heap string: %s", str->chars);
                pool_free(context->heap->pool, (void*)str);
            }
        }
        else if (itm._type_id == LMD_TYPE_DECIMAL) {
            Decimal *dec = itm.get_decimal();
            if (dec && !dec->ref_cnt) {
                log_debug("freeing heap decimal");
                pool_free(context->heap->pool, (void*)dec);
            }
        }
        else if (itm._type_id == LMD_TYPE_RAW_POINTER) {
            Container* cont = itm.container;
            if (cont) {
                // check if this is a Function (type_id at offset 0)
                TypeId type_id = cont->type_id;
                if (type_id == LMD_TYPE_FUNC) {
                    // Function has different layout than Container
                    Function* fn = (Function*)cont;
                    if (fn->ref_cnt > 0) {
                        // still referenced, keep in heap
                        entries->data[i] = NULL;
                    } else {
                        // no references, free it
                        log_debug("freeing heap function: %p", fn);
                        pool_free(context->heap->pool, fn);
                    }
                }
                else if (cont->ref_cnt > 0) {
                    // clear the heap entry, and keep the container to be freed by ref_cnt
                    entries->data[i] = NULL;
                }
                else free_container(cont, false);
            }
        }
        else if (itm._type_id == LMD_CONTAINER_HEAP_START) {
            log_debug("reached container start: %d", i);
            size_t stack_pos = (size_t)(((uint64_t)entries->data[i-1]) & 0x00FFFFFFFFFFFFFF);
            num_stack_reset_to_index((num_stack_t*)context->num_stack, stack_pos);
            entries->length = i-1;
            log_debug("reset num stack to index: %zu, new entries length: %d", stack_pos, entries->length);
            return;
        }
    }
    log_debug("end of frame_end with entries: %d", entries->length);
}
