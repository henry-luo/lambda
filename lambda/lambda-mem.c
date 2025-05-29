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
    }
}

void check_heap_entries() {
    ArrayList *entries = context->heap->entries;
    printf("heap entries: %d\n", entries->length);
    for (int i = 0; i < entries->length; i++) {
        void *data = entries->data[i];
        LambdaItem itm = {.raw_pointer = data};
        printf("heap entry index: %d, type: %d, data: %p\n", i, itm.type_id, data);
    }
}

void free_container(Container* cont) {
    printf("freeing data owner: %p\n", cont);
    TypeId type_id = cont->type_id;
    if (type_id == LMD_TYPE_LIST) {
        List *list = (List *)cont;
        if (!list->ref_cnt) {
            // free list items
            printf("freeing list items: %p, length: %ld\n", list, list->length);
            for (int j = 0; j < list->length; j++) {
                free_item(list->items[j], true);
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
                free_item(arr->items[j], true);
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
    else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
        Map *map = (Map*)cont;
        if (!map->ref_cnt) {
            // free map items based on the shape
            ShapeEntry *field = ((LambdaTypeMap*)map->type)->shape;
            printf("freeing map items: %p, length: %d\n", map, ((LambdaTypeMap*)map->type)->length);
            while (field) {
                printf("freeing map field: %s, type: %d\n", field->name.str, field->type->type_id);
                void* field_ptr = ((uint8_t*)map->data) + field->byte_offset;
                if (field->type->type_id == LMD_TYPE_STRING || field->type->type_id == LMD_TYPE_SYMBOL || 
                    field->type->type_id == LMD_TYPE_DTIME || field->type->type_id == LMD_TYPE_BINARY) {
                    String *str = *(String**)field_ptr;
                    Item item = s2it(str);
                    free_item(item, true);
                }
                else if (field->type->type_id == LMD_TYPE_ARRAY || field->type->type_id == LMD_TYPE_LIST || 
                    field->type->type_id == LMD_TYPE_MAP || field->type->type_id == LMD_TYPE_ELEMENT) {
                    Container *container = *(Container**)field_ptr;
                    free_container(container);
                }
                field = field->next;
            }
            printf("freeing map data: %p\n", map);
            if (map->data) free(map->data);
            pool_variable_free(context->heap->pool, cont);
        }
    }
}

void free_item(Item item, bool free_mapping) {
    LambdaItem itm = {.item = item};
    printf("free item: type: %d, pointer: %llu\n", itm.type_id, itm.pointer);
    if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_DTIME || itm.type_id == LMD_TYPE_BINARY) {
        String *str = (String*)itm.pointer;
        DataOwner *owned = (DataOwner*)hashmap_get(context->data_owners, &(DataOwner){.data = str});
        if (owned) {
            Container* owner = owned->owner;
            owner->ref_cnt--;
            // the data owner entry might be shared
            if (free_mapping) hashmap_delete(context->data_owners, &str);
            // let heap entry free the owner
        }
        else if (str->contained) {
            pool_variable_free(context->heap->pool, str);
        }
    }
    else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        free_container((Container*)itm.raw_pointer);
    }
}

void retain_string(String *str) {
    if (str->heap_owned) {  // remove string from heap entries
        str->heap_owned = false;  str->contained = true;  // change ownership from heap to container
        int entry = context->heap->entries->length-1;  // int start = context->heap->entry_start->start;
        for (; entry >= 0; entry--) {
            void *data = context->heap->entries->data[entry];
            LambdaItem itm = {.raw_pointer = data};
            if (itm.type_id == LMD_TYPE_STRING && itm.pointer == str) {
                printf("removing string from heap entries: %p\n", str);
                context->heap->entries->data[entry] = NULL;  break;
            }
            else if (itm.type_id == LMD_TYPE_CONTAINER_START) {
                printf("found container start, stop searching\n");
                break;  // stop searching
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
            pool_variable_free(context->heap->pool, (void*)itm.pointer);
        }
        else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
            free_container((Container*)itm.raw_pointer);
        }
        else if (itm.type_id == LMD_TYPE_CONTAINER_START) {
            printf("reached container start: %d\n", i);
            entries->length = i;
            return;
        }
    }
}