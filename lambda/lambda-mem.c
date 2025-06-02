#include "transpiler.h"

extern __thread Context* context;

int dataowner_compare(const void *a, const void *b, void *udata) {
    const DataOwner *da = a;
    const DataOwner *db = b;
    return da->data == db->data;
}

uint64_t dataowner_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const DataOwner *dataowner = item;
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
    arraylist_append(heap->entries, type_id < LMD_TYPE_ARRAY ?
        ((((uint64_t)type_id)<<56) | (uint64_t)(data)) : data);
    return data;
}

void* heap_calloc(size_t size, TypeId type_id) {
    void* ptr = heap_alloc(size, type_id);
    memset(ptr, 0, size);
    return ptr;
}

void heap_destroy() {
    if (context->heap) {
        if (context->heap->pool) pool_variable_destroy(context->heap->pool);
        free(context->heap);
    }
}

Item HEAP_ENTRY_START = ((uint64_t)LMD_TYPE_CONTAINER_START << 56); 

void print_heap_entries() {
    ArrayList *entries = context->heap->entries;
    printf("after exec heap entries: %d\n", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        LambdaItem itm = {.raw_pointer = data};
        printf("heap entry index: %d, type: %d, data: %p\n", i, itm.type_id, data);
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)data);
            printf("heap entry data: type: %s\n", type_info[type_id].name);
            if (type_id == LMD_TYPE_LIST || type_id == LMD_TYPE_ARRAY || 
                type_id == LMD_TYPE_ARRAY_INT || type_id == LMD_TYPE_MAP || 
                type_id == LMD_TYPE_ELEMENT) {
                Container *cont = (Container*)data;
                printf("heap entry container: type: %s, ref_cnt: %d\n", 
                    type_info[type_id].name, cont->ref_cnt);
            }
        }        
    }
}

void check_memory_leak() {
    StrBuf *strbuf = strbuf_new(1024);
    ArrayList *entries = context->heap->entries;
    printf("check heap entries: %d\n", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        LambdaItem itm = {.raw_pointer = data};
        printf("heap entry index: %d, type: %s, data: %p\n", i, type_info[itm.type_id].name, data);
        if (!data) { continue; }  // skip NULL entries
        if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            TypeId type_id = *((uint8_t*)itm.raw_pointer);
            printf("heap entry data: type: %s\n", type_info[type_id].name);
            if (type_id == LMD_TYPE_LIST) {
                List *list = (List*)data;
                printf("heap entry list: %p, length: %ld, ref_cnt: %d\n", list, list->length, list->ref_cnt);
                strbuf_reset(strbuf);
                print_item(strbuf, itm.item);
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
            else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
                Map *map = (Map*)data;
                printf("heap entry map: %p, length: %ld, ref_cnt: %d\n", map, 
                    ((LambdaTypeMap*)map->type)->length, map->ref_cnt);
            }
        }
    }  
}

void free_container(Container* cont, bool clear_entry);

void free_map_item(ShapeEntry *field, void* map_data, bool clear_entry) {
    while (field) {
        // printf("freeing map field: %.*s, type: %d\n", (int)field->name.length, field->name.str, field->type->type_id);
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        if (field->type->type_id == LMD_TYPE_STRING || field->type->type_id == LMD_TYPE_SYMBOL || 
            field->type->type_id == LMD_TYPE_DTIME || field->type->type_id == LMD_TYPE_BINARY) {
            String *str = *(String**)field_ptr;
            Item item = s2it(str);
            free_item(item, clear_entry);
        }
        else if (field->type->type_id == LMD_TYPE_ARRAY || field->type->type_id == LMD_TYPE_LIST || 
            field->type->type_id == LMD_TYPE_MAP || field->type->type_id == LMD_TYPE_ELEMENT) {
            Container *container = *(Container**)field_ptr;
            // delink with the container
            if (container->ref_cnt > 0) container->ref_cnt--;
            if (!container->ref_cnt) free_container(container, clear_entry);
        }
        field = field->next;
    }
}

void free_container(Container* cont, bool clear_entry) {
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
            printf("freeing array items: %p, length: %ld\n", arr, arr->length);
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
            printf("freeing array int items: %p, length: %ld\n", arr, arr->length);
            if (arr->items) free(arr->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_MAP) {
        Map *map = (Map*)cont;
        if (!map->ref_cnt) {
            // free map items based on the shape
            ShapeEntry *field = ((LambdaTypeMap*)map->type)->shape;
            printf("freeing map items: %p, length: %ld\n", map, ((LambdaTypeMap*)map->type)->length);
            if (field) { free_map_item(field, map->data, clear_entry); }
            if (map->data) free(map->data);
            pool_variable_free(context->heap->pool, cont);
        }
    }
    else if (type_id == LMD_TYPE_ELEMENT) {
        Element *elmt = (Element*)cont;
        if (!elmt->ref_cnt) {
            // free element attrs based on the shape
            ShapeEntry *field = ((LambdaTypeElmt*)elmt->type)->shape;
            printf("freeing element items: %p, length: %ld\n", elmt, ((LambdaTypeElmt*)elmt->type)->length);
            if (field) { free_map_item(field, elmt->data, clear_entry); }
            if (elmt->data) free(elmt->data);
            // free content
            printf("freeing element content: %p, length: %ld\n", elmt, elmt->length);
            for (long j = 0; j < elmt->length; j++) {
                free_item(elmt->items[j], clear_entry);
            }
            if (elmt->items) free(elmt->items);
            pool_variable_free(context->heap->pool, cont);
        }
    }    
}

void free_item(Item item, bool clear_entry) {
    LambdaItem itm = {.item = item};
    // printf("free item: type: %d, pointer: %llu\n", itm.type_id, itm.pointer);
    if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_DTIME || itm.type_id == LMD_TYPE_BINARY) {
        String *str = (String*)itm.pointer;
        if (!str->ref_cnt) {
            pool_variable_free(context->heap->pool, str);
        }
    }
    else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        Container* container = (Container*)itm.raw_pointer;
        // delink with the container
        if (container->ref_cnt > 0) container->ref_cnt--;
        if (!container->ref_cnt) free_container(container, clear_entry);
    }
    if (clear_entry) {
        ArrayList *entries = context->heap->entries;
        // remove the entry from heap entries
        for (int i = entries->length - 1; i >= 0; i--) {
            void *data = entries->data[i];
            if (data == item) { 
                entries->data[i] = NULL;  break;
            }
        }
    }
}

void entry_start() {
    arraylist_append(context->heap->entries, (void*)HEAP_ENTRY_START);
}

void entry_end() {
    ArrayList *entries = context->heap->entries;
    // free heap allocations
    for (int i = entries->length - 1; i >= 0; i--) {
        printf("free heap entry index: %d\n", i);
        void *data = entries->data[i];
        if (!data) { continue; }  // skip NULL entries
        LambdaItem itm = {.raw_pointer = data};
        if (itm.type_id == LMD_TYPE_STRING) {
            String *str = (String*)itm.pointer;
            if (!str->ref_cnt) {
                printf("freeing heap string: %s\n", str->chars);
                pool_variable_free(context->heap->pool, (void*)str);
            }
        }
        else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            Container* cont = (Container*)itm.raw_pointer;
            if (cont->ref_cnt > 0) {
                // clear the heap entry, and keep the container to be freed by ref_cnt
                entries->data[i] = NULL;  
            }
            else free_container(cont, false);
        }
        else if (itm.type_id == LMD_TYPE_CONTAINER_START) {
            printf("reached container start: %d\n", i);
            entries->length = i;
            return;
        }
    }
}