#include "transpiler.hpp"

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
    printf("heap init: %p\n", context);
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
        printf("Error: Failed to allocate memory for heap entry.\n");
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

void expand_list(List *list) {
    //printf("expand list: %p, length: %ld, capacity: %ld\n", 
    //    list, list->length, list->capacity);
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    // list items are allocated from C heap, instead of Lambda heap
    // to consider: could also alloc directly from Lambda heap without the heap entry
    // need to profile to see which is faster
    Item* old_items = list->items;
    list->items = (Item*)realloc(list->items, list->capacity * sizeof(Item));
    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra), 
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            Item itm = list->items[i];
            if (itm.type_id == LMD_TYPE_FLOAT || itm.type_id == LMD_TYPE_INT64 ||
                itm.type_id == LMD_TYPE_DTIME) {
                Item* old_pointer = (Item*)itm.pointer;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = {.item = itm.type_id == LMD_TYPE_FLOAT ? d2it(new_pointer) : 
                        itm.type_id == LMD_TYPE_INT64 ? l2it(new_pointer) : k2it(new_pointer)};
                }
                // if the pointer is not in the old buffer, it should not be updated
            }
        }
    }
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
    printf("after exec heap entries: %d\n", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        Item itm = {.raw_pointer = data};
        printf("heap entry index: %d, type: %d, data: %p\n", i, itm.type_id, data);
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)data);
            printf("heap entry data: type: %s\n", type_info[type_id].name);
            if (type_id == LMD_TYPE_LIST || type_id == LMD_TYPE_ARRAY || 
                type_id == LMD_TYPE_ARRAY_INT || type_id == LMD_TYPE_ARRAY_FLOAT || type_id == LMD_TYPE_MAP || 
                type_id == LMD_TYPE_ELEMENT) {
                Container *cont = (Container*)data;
                printf("heap entry container: type: %s, ref_cnt: %d\n", 
                    type_info[type_id].name, cont->ref_cnt);
            }
        }        
    }
}

void check_memory_leak() {
    StrBuf *strbuf = strbuf_new_cap(1024);
    ArrayList *entries = context->heap->entries;
    printf("check heap entries: %d\n", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        Item itm = {.raw_pointer = data};
        printf("heap entry index: %d, type: %s, data: %p\n", i, type_info[itm.type_id].name, data);
        if (!data) { continue; }  // skip NULL entries
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)itm.raw_pointer);
            printf("heap entry data: type: %s\n", type_info[type_id].name);
            if (type_id == LMD_TYPE_LIST) {
                List *list = (List*)data;
                printf("heap entry list: %p, length: %ld, ref_cnt: %d\n", list, list->length, list->ref_cnt);
                strbuf_reset(strbuf);
                print_item(strbuf, itm);
                printf("heap entry list: %s\n", strbuf->str);
            }
            else if (type_id == LMD_TYPE_ARRAY) {
                Array *arr = (Array*)data;
                printf("heap entry array: %p, length: %ld, ref_cnt: %d\n", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_ARRAY_INT) {
                ArrayLong *arr = (ArrayLong*)data;
                printf("heap entry array int: %p, length: %ld, ref_cnt: %d\n", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
                ArrayFloat *arr = (ArrayFloat*)data;
                printf("heap entry array float: %p, length: %ld, ref_cnt: %d\n", arr, arr->length, arr->ref_cnt);
            }
            else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
                Map *map = (Map*)data;
                printf("heap entry map: %p, length: %ld, ref_cnt: %d\n", map, 
                    ((TypeMap*)map->type)->length, map->ref_cnt);
            }
        }
    }  
}

void free_container(Container* cont, bool clear_entry);

void free_map_item(ShapeEntry *field, void* map_data, bool clear_entry) {
    while (field) {
        // printf("freeing map field: %.*s, type: %d\n", (int)field->name.length, field->name.str, field->type->type_id);
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
        else if (field->type->type_id == LMD_TYPE_ARRAY || field->type->type_id == LMD_TYPE_LIST || 
            field->type->type_id == LMD_TYPE_MAP || field->type->type_id == LMD_TYPE_ELEMENT) {
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
    if (!cont) return;  // Add null pointer check
    printf("free container: %p\n", cont);
    assert(cont->ref_cnt == 0);
    TypeId type_id = cont->type_id;
    if (type_id == LMD_TYPE_LIST) {
        List *list = (List *)cont;
        if (!list->ref_cnt) {
            // free list items
            printf("freeing list items: %p, length: %ld\n", list, list->length);
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
        ArrayLong *arr = (ArrayLong*)cont;
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
    size_t stack_pos = ((num_stack_t *)context->num_stack)->current_position;
    arraylist_append(context->heap->entries, (void*) (((uint64_t)LMD_CONTAINER_HEAP_START << 56) | stack_pos));
    arraylist_append(context->heap->entries, HEAP_ENTRY_START.raw_pointer);
}

void frame_end() {
    ArrayList *entries = context->heap->entries;
    printf("entering frame_end with entries: %d\n", entries->length);
    // free heap allocations
    int loop_count = 0;
    int original_length = entries->length;
    for (int i = entries->length - 1; i >= 0; i--) {
        printf("frame_end loop: %d, i: %d, original_length: %d\n", loop_count, i, original_length);
        loop_count++;
        if (loop_count > original_length + 100) {
            printf("ERROR: frame_end infinite loop detected! loop_count=%d, i=%d, original_length=%d\n", 
                   loop_count, i, original_length);
            break;
        }
        printf("free heap entry index: %d\n", i);
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        Item itm = {.raw_pointer = data};
        if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
            itm.type_id == LMD_TYPE_BINARY) {
            String *str = (String*)itm.pointer;
            if (str && !str->ref_cnt) {
                printf("freeing heap string: %s\n", str->chars);
                pool_variable_free(context->heap->pool, (void*)str);
            }
        }
        else if (itm.type_id == LMD_TYPE_DECIMAL) {
            Decimal *dec = (Decimal*)itm.pointer;
            if (dec && !dec->ref_cnt) {
                printf("freeing heap decimal\n");
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
            printf("reached container start: %d\n", i);
            size_t stack_pos = (size_t)(((uint64_t)entries->data[i-1]) & 0x00FFFFFFFFFFFFFF);
            num_stack_reset_to_index((num_stack_t*)context->num_stack, stack_pos);
            entries->length = i-1;
            printf("reset num stack to index: %zu, new entries length: %d\n", stack_pos, entries->length);
            return;
        }
    }
    printf("end of frame_end with entries: %d\n", entries->length);
}