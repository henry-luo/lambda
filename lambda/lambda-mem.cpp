#include "transpiler.hpp"
#include "../lib/log.h"

extern __thread Context* context;

int dataowner_compare(const void *a, const void *b, void *udata) {
    const DataOwner *da = (const DataOwner *)a;
    const DataOwner *db = (const DataOwner *)b;
    return da->data == db->data;
}

uint64_t dataowner_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const DataOwner *dataowner = (const DataOwner *)item;
    return hashmap_xxhash3(dataowner->data, sizeof(dataowner->data), seed0, seed1);
}

void heap_init() {
    log_debug("heap init: %p", context);
    context->heap = (Heap*)calloc(1, sizeof(Heap));
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    pool_variable_init(&context->heap->pool, grow_size, tolerance_percent);
    context->heap->entries = arraylist_new(1024);
}

void* heap_alloc(size_t size, TypeId type_id) {
    Heap *heap = context->heap;
    void *data;
    pool_variable_alloc(heap->pool, size, (void**)&data);
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

void* heap_calloc(size_t size, TypeId type_id) {
    void* ptr = heap_alloc(size, type_id);
    memset(ptr, 0, size);
    return ptr;
}

String* heap_string(char* src, int len) {
    String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
    strcpy(str->chars, src);
    str->len = len;  str->ref_cnt = 0;    
    return str;
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

Item push_l(long lval) {
    log_debug("push_l: %ld", lval);
    // Safety check: if context is num_stack is NULL
    if (!context->num_stack) {
        log_error("push_l called with invalid context");
        return ItemError;
    }
    if (lval == INT_ERROR) return ItemError;
    long *lptr = num_stack_push_long(context->num_stack, lval);
    return {.item = l2it(lptr)};
}

Item push_k(DateTime val) {
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
        if (context->heap->pool) pool_variable_destroy(context->heap->pool);
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
        Item itm = {.raw_pointer = data};
        log_debug("heap entry index: %d, type: %d, data: %p", i, itm.type_id, data);
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
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
        void *data = entries->data[i];
        Item itm = {.raw_pointer = data};
        log_debug("heap entry index: %d, type: %s, data: %p", i, type_info[itm.type_id].name, data);
        if (!data) { continue; }  // skip NULL entries
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)itm.raw_pointer);
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
    while (field) {
        // log_debug("freeing map field: %.*s, type: %d", (int)field->name.length, field->name.str, field->type->type_id);
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        if (!field->name) { // nested map
            Map *nested_map = *(Map**)field_ptr;
            if (nested_map) {
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
            if (container) {
                // delink with the container
                if (container->ref_cnt > 0) container->ref_cnt--;
                if (!container->ref_cnt) free_container(container, clear_entry);
            }
        }
        field = field->next;
    }
}

void free_container(Container* cont, bool clear_entry) {
    if (!cont) return;
    log_debug("free container: %p", cont);
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
            if (list->items) free(list->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY) {
        Array *arr = (Array*)cont;
        if (!arr->ref_cnt) {
            // free array items
            for (int j = 0; j < arr->length; j++) {
                free_item(arr->items[j], clear_entry);
            }
            if (arr->items) free(arr->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt *arr = (ArrayInt*)cont;
        if (!arr->ref_cnt) {
            if (arr->items) free(arr->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64 *arr = (ArrayInt64*)cont;
        if (!arr->ref_cnt) {
            if (arr->items) free(arr->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat *arr = (ArrayFloat*)cont;
        if (!arr->ref_cnt) {
            if (arr->items) free(arr->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_MAP) {
        Map *map = (Map*)cont;
        if (!map->ref_cnt) {
            // free map items based on the shape
            ShapeEntry *field = ((TypeMap*)map->type)->shape;
            if (field) { free_map_item(field, map->data, clear_entry); }
            if (map->data) free(map->data);
            pool_variable_free(context->heap->pool, cont);
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
            for (long j = 0; j < elmt->length; j++) {
                free_item(elmt->items[j], clear_entry);
            }
            if (elmt->items) free(elmt->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }    
}

void free_item(Item item, bool clear_entry) {
    if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL || 
        item.type_id == LMD_TYPE_BINARY) {
        String *str = (String*)item.pointer;
        if (str && !str->ref_cnt) {
            pool_variable_free(context->heap->pool, str);
        }
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        Decimal *dec = (Decimal*)item.pointer;
        if (dec && !dec->ref_cnt) {
            pool_variable_free(context->heap->pool, dec);
        }
    }
    else if (item.type_id == LMD_TYPE_RAW_POINTER) {
        Container* container = item.container;
        if (container) {
            // delink with the container
            if (container->ref_cnt > 0) container->ref_cnt--;
            if (!container->ref_cnt) free_container(container, clear_entry);
        }
    }
    if (clear_entry) {
        ArrayList *entries = context->heap->entries;
        // remove the entry from heap entries
        for (int i = entries->length - 1; i >= 0; i--) {
            void *data = entries->data[i];
            if (data == item.raw_pointer) { 
                entries->data[i] = NULL;  break;
            }
        }
    }
} 

void frame_start() {
    size_t stack_pos = context->num_stack->total_length;
    log_debug("entering frame_start with num stack position: %zu", stack_pos);
    arraylist_append(context->heap->entries, (void*) (((uint64_t)LMD_CONTAINER_HEAP_START << 56) | stack_pos));
    arraylist_append(context->heap->entries, HEAP_ENTRY_START.raw_pointer);
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
        Item itm = {.raw_pointer = data};
        if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
            itm.type_id == LMD_TYPE_BINARY) {
            String *str = (String*)itm.pointer;
            if (str && !str->ref_cnt) {
                log_debug("freeing heap string: %s", str->chars);
                pool_variable_free(context->heap->pool, (void*)str);
            }
        }
        else if (itm.type_id == LMD_TYPE_DECIMAL) {
            Decimal *dec = (Decimal*)itm.pointer;
            if (dec && !dec->ref_cnt) {
                log_debug("freeing heap decimal");
                pool_variable_free(context->heap->pool, (void*)dec);
            }
        }
        else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            Container* cont = (Container*)itm.raw_pointer;
            if (cont) {
                if (cont->ref_cnt > 0) {
                    // clear the heap entry, and keep the container to be freed by ref_cnt
                    entries->data[i] = NULL;  
                }
                else free_container(cont, false);
            }
        }
        else if (itm.type_id == LMD_CONTAINER_HEAP_START) {
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