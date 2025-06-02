#include "transpiler.h"
#include <stdarg.h>

extern __thread Context* context;

#define stack_alloc(size) pack_alloc(context->stack, size)
#define Malloc malloc 

Array* array() {
    Array *arr = calloc(1, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    entry_start();
    return arr;
}

Array* array_fill(Array* arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->capacity = count;
        arr->items = Malloc(count * sizeof(Item));
        for (int i = 0; i < count; i++) {
            Item item = arr->items[i] = va_arg(args, Item);
            LambdaItem itm = {.item = item};
            if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
                itm.type_id == LMD_TYPE_DTIME || itm.type_id == LMD_TYPE_BINARY) {
                String *str = (String*)itm.pointer;
                str->ref_cnt++;
            }
            else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
                TypeId type_id = *((uint8_t*)itm.raw_pointer);
                if (type_id == LMD_TYPE_LIST || type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_INT || 
                    type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
                    Container *container = (Container*)itm.raw_pointer;
                    container->ref_cnt++;
                }
            }
        }
        arr->length = count;
        va_end(args);
    }
    entry_end();
    return arr;
}

Item array_get(Array *array, int index) {
    if (index < 0 || index >= array->length) { return ITEM_NULL; }
    return array->items[index];
}

ArrayLong* array_long_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayLong *arr = heap_alloc(sizeof(ArrayLong), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;  arr->capacity = count;
    arr->items = Malloc(count * sizeof(long));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, long);
        printf("array int: %ld\n", arr->items[i]);
    }       
    va_end(args);
    return arr;
}

List* list() {
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    list->type_id = LMD_TYPE_LIST;
    entry_start();
    return list;
}

void list_push(List *list, Item item) {
    if (!item) { return; }  // NULL value
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_NULL) { 
        return;  // skip NULL value
    }
    if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        TypeId type_id = *((uint8_t*)itm.raw_pointer);
        // nest list is flattened
        if (type_id == LMD_TYPE_LIST) {
            // copy over the items
            List *nest_list = (List*)itm.raw_pointer;
            for (int i = 0; i < nest_list->length; i++) {
                Item nest_item = nest_list->items[i];
                list_push(list, nest_item);
            }
            return;
        }
        else if (type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_INT || 
            type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
            Container *container = (Container*)itm.raw_pointer;
            container->ref_cnt++;
        }
    }
    // store the value in the list
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        // list items are allocated from C heap, instead of Lambda heap
        // to consider: could also alloc from directly from Lambda heap without the heap entry
        // need to profile to see which is faster
        list->items = realloc(list->items, list->capacity * sizeof(Item));
    }
    list->items[list->length++] = item;
    printf("list push item: %llu, type: %d, length: %ld\n", item, itm.type_id, list->length);
    // data ownership handling
    if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_DTIME || itm.type_id == LMD_TYPE_BINARY) {
        String *str = (String*)itm.pointer;
        str->ref_cnt++;
    }
}

Item list_get(List *list, int index) {
    if (index < 0 || index >= list->length) { return ITEM_NULL; }
    return list->items[index];
}

List* list_fill(List *list, int count, ...) {
    printf("list_fill cnt: %d\n", count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        LambdaItem itm = {.item = va_arg(args, uint64_t)};
        if (itm.type_id == LMD_TYPE_NULL) { 
            continue;  // skip NULL value
        }
        list_push(list, itm.item);
    }
    va_end(args);
    entry_end();
    return list;
}

void set_fields(LambdaTypeMap *map_type, void* map_data, va_list args) {
    int count = map_type->length;
    printf("map length: %d\n", count);
    ShapeEntry *field = map_type->shape;
    for (int i = 0; i < count; i++) {
        printf("field type: %d, offset: %d\n", field->type->type_id, field->byte_offset);
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                *(bool*)field_ptr = va_arg(args, bool);
                break;
            case LMD_TYPE_BOOL:
                *(bool*)field_ptr = va_arg(args, bool);
                printf("field bool value: %s\n", *(bool*)field_ptr ? "true" : "false");
                break;                
            case LMD_TYPE_IMP_INT:  case LMD_TYPE_INT:
                *(long*)field_ptr = va_arg(args, long);
                printf("field int value: %ld\n", *(long*)field_ptr);
                break;
            case LMD_TYPE_FLOAT:
                *(double*)field_ptr = va_arg(args, double);
                printf("field float value: %f\n", *(double*)field_ptr);
                break;
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  
            case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
                String *str = va_arg(args, String*);
                printf("field string value: %s\n", str->chars);
                *(String**)field_ptr = str;
                str->ref_cnt++;
                break;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:
                Container *container = va_arg(args, Container*);
                *(Container**)field_ptr = container;
                container->ref_cnt++;
                break;            
            case LMD_TYPE_FUNC:  case LMD_TYPE_ANY:
                void *arr = va_arg(args, void*);
                *(void**)field_ptr = arr;
                break;
            default:
                printf("unknown type %d\n", field->type->type_id);
        }
        field = field->next;
    }
}

Map* map(int type_index) {
    printf("map with type %d\n", type_index);
    Map *map = (Map *)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    map->type_id = LMD_TYPE_MAP;
    ArrayList* type_list = (ArrayList*)context->type_list;
    AstMapNode* node = (AstMapNode*)((AstNode*)type_list->data[type_index]);
    LambdaTypeMap *map_type = (LambdaTypeMap*)node->type;
    map->type = map_type;    
    entry_start();
    return map;
}

// zig cc has problem compiling this function, it seems to align the pointers to 8 bytes
Map* map_fill(Map* map, ...) {
    LambdaTypeMap *map_type = (LambdaTypeMap*)map->type;
    map->data = calloc(1, map_type->byte_size);
    printf("map byte_size: %d\n", map_type->byte_size);
    // set map fields
    va_list args;
    va_start(args, map_type->length);
    set_fields(map_type, map->data, args);
    va_end(args);
    entry_end();
    return map;
}

Element* elmt(int type_index) {
    printf("elmt with type %d\n", type_index);
    Element *elmt = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    elmt->type_id = LMD_TYPE_ELEMENT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    AstElementNode* node = (AstElementNode*)((AstNode*)type_list->data[type_index]);
    LambdaTypeElmt *elmt_type = (LambdaTypeElmt*)node->type;
    elmt->type = elmt_type;
    if (elmt_type->length || elmt_type->content_length) {
        entry_start();
    }
    return elmt;
}

Element* elmt_fill(Element* elmt, ...) {
    LambdaTypeElmt *elmt_type = (LambdaTypeElmt*)elmt->type;
    elmt->data = calloc(1, elmt_type->byte_size);  // heap_alloc(rt->heap, elmt_type->byte_size);
    printf("elmt byte_size: %d\n", elmt_type->byte_size);
    // set attributes
    int count = elmt_type->length;
    printf("elmt length: %d\n", count);
    va_list args;
    va_start(args, count);
    set_fields((LambdaTypeMap*)elmt_type, elmt->data, args);
    va_end(args);
    return elmt;
}

Item map_get(Map* map, char *key) {
    if (!map || !key) { return ITEM_NULL; }
    ShapeEntry *field = ((LambdaTypeMap*)map->type)->shape;
    while (field) {
        if (strncmp(field->name.str, key, field->name.length) == 0) {
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map->data + field->byte_offset;
            switch (type_id) {
                case LMD_TYPE_NULL:
                    return ITEM_NULL;
                case LMD_TYPE_BOOL:
                    return b2it(*(bool*)field_ptr);
                case LMD_TYPE_IMP_INT:
                    return i2it(*(int*)field_ptr);
                case LMD_TYPE_FLOAT:
                    double dval = *(double*)field_ptr;
                    return push_d(dval);
                case LMD_TYPE_DTIME:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return k2it(*(char**)field_ptr);
                case LMD_TYPE_STRING:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return s2it(*(char**)field_ptr);
                case LMD_TYPE_SYMBOL:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return y2it(*(char**)field_ptr);
                case LMD_TYPE_BINARY:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return x2it(*(char**)field_ptr);
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
                case LMD_TYPE_LIST:  case LMD_TYPE_MAP:
                    return (Item)*(Map**)field_ptr;
                default:
                    printf("unknown type %d\n", type_id);
                    return ITEM_ERROR;
            }
        }
        field = field->next;
    }
    printf("key %s not found\n", key);
    return ITEM_NULL;
}

bool item_true(Item itm) {
    LambdaItem item = {.item = itm};
    printf("item type: %d, val: %llu\n", item.type_id, item.pointer);
    switch (item.type_id) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_ERROR:
        return false;
    case LMD_TYPE_BOOL:
        return item.bool_val;
    default:
        return true;
    }
}

// list to item
Item v2it(List* list) {
    if (!list) { return ITEM_NULL; }
    printf("v2it %p, length: %ld\n", list, list->length);
    if (list->length == 0) { return ITEM_NULL; }
    if (list->length == 1) { return list->items[0]; }
    return (Item)list;
}

Item push_d(double dval) {
    printf("push_d: %g\n", dval);
    double *dptr = stack_alloc(sizeof(double));
    *dptr = dval;
    return (Item) d2it(dptr);
}

Item push_l(long lval) {
    printf("push_l: %ld\n", lval);
    long *lptr = stack_alloc(sizeof(long));
    *lptr = lval;
    return (Item) l2it(lptr);
}

String *str_cat(String *left, String *right) {
    printf("str_cat %p, %p\n", left, right);
    size_t left_len = left->len;
    size_t right_len = right->len;
    printf("left len %zu, right len %zu\n", left_len, right_len);
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    printf("str result %p\n", result);
    result->ref_cnt = 0;  result->len = left_len + right_len;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    printf("str_cat result: %s\n", result->chars);
    return result;
}

Item add(Context *rt, Item a, Item b) {
    LambdaItem item_a = {.item = a};  LambdaItem item_b = {.item = b};
    // todo: join binary, list, array, map
    if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = str_cat(str_a, str_b);
        return s2it(result);
    }
    else if (item_a.type_id == LMD_TYPE_IMP_INT && item_b.type_id == LMD_TYPE_IMP_INT) {
        return i2it(item_a.long_val + item_b.long_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return l2it(push_l(*(long*)item_a.pointer + *(long*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("add float: %g + %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return d2it(push_d(*(double*)item_a.pointer + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_IMP_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return d2it(push_d((double)item_a.long_val + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_IMP_INT) {
        return d2it(push_d(*(double*)item_a.pointer + (double)item_b.long_val));
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ITEM_ERROR;
}

long it2l(Item item) {
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_IMP_INT) {
        return itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        return *(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return (long)*(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

double it2d(Item item) {
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_IMP_INT) {
        return (double)itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        return (double)*(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return *(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

Function* to_fn(fn_ptr ptr) {
    printf("create fn %p\n", ptr);
    Function *fn = calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = ptr;
    return fn;
}

extern LambdaTypeType LIT_TYPE_NULL;
extern LambdaTypeType LIT_TYPE_BOOL;
extern LambdaTypeType LIT_TYPE_INT;
extern LambdaTypeType LIT_TYPE_FLOAT;
extern LambdaTypeType LIT_TYPE_NUMBER;
extern LambdaTypeType LIT_TYPE_STRING;
extern LambdaTypeType LIT_TYPE_BINARY;
extern LambdaTypeType LIT_TYPE_SYMBOL;
extern LambdaTypeType LIT_TYPE_DTIME;
extern LambdaTypeType LIT_TYPE_LIST;
extern LambdaTypeType LIT_TYPE_ARRAY;
extern LambdaTypeType LIT_TYPE_MAP;
extern LambdaTypeType LIT_TYPE_ELMT;
extern LambdaTypeType LIT_TYPE_FUNC;
extern LambdaTypeType LIT_TYPE_TYPE;
extern LambdaTypeType LIT_TYPE_ANY;
extern LambdaTypeType LIT_TYPE_ERROR;

LambdaType *type_null() {
    return (LambdaType *)&LIT_TYPE_NULL;
}
LambdaType *type_bool() {
    return (LambdaType *)&LIT_TYPE_BOOL;
}
LambdaType *type_int() {
    return (LambdaType *)&LIT_TYPE_INT;
}
LambdaType *type_float() {
    return (LambdaType *)&LIT_TYPE_FLOAT;
}
LambdaType *type_number() {
    return (LambdaType *)&LIT_TYPE_NUMBER;
}
LambdaType *type_string() {
    return (LambdaType *)&LIT_TYPE_STRING;
}
LambdaType *type_binary() {
    return (LambdaType *)&LIT_TYPE_BINARY;
}
LambdaType *type_symbol() {
    return (LambdaType *)&LIT_TYPE_SYMBOL;
}
LambdaType *type_dtime() {
    return (LambdaType *)&LIT_TYPE_DTIME;
}
LambdaType *type_list() {
    return (LambdaType *)&LIT_TYPE_LIST;
}
LambdaType *type_array() {
    return (LambdaType *)&LIT_TYPE_ARRAY;
}
LambdaType *type_map() {
    return (LambdaType *)&LIT_TYPE_MAP;
}
LambdaType *type_elmt() {
    return (LambdaType *)&LIT_TYPE_ELMT;
}
LambdaType *type_func() {
    return (LambdaType *)&LIT_TYPE_FUNC;
}
LambdaType *type_type() {
    return (LambdaType *)&LIT_TYPE_TYPE;
}
LambdaType *type_any() {
    return (LambdaType *)&LIT_TYPE_ANY;
}
LambdaType *type_error() {
    return (LambdaType *)&LIT_TYPE_ERROR;
}

bool is(Item a, Item b) {
    printf("is expr\n");
    LambdaItem a_item = {.item = a};
    LambdaItem b_item = {.item = b};
    if (b_item.type_id || *((uint8_t*)b) != LMD_TYPE_TYPE) {
        return false;
    }
    LambdaTypeType *type_b = (LambdaTypeType *)b;
    printf("is type %d, %d\n", a_item.type_id, type_b->type->type_id);
    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        return a_item.type_id != LMD_TYPE_ERROR;
    case LMD_TYPE_IMP_INT:  case LMD_TYPE_INT:  case LMD_TYPE_FLOAT:  case LMD_TYPE_NUMBER:
        return LMD_TYPE_IMP_INT <= a_item.type_id && a_item.type_id <= type_b->type->type_id;
    default:
        return a_item.type_id ? a_item.type_id == type_b->type->type_id : *((uint8_t*)a) == type_b->type->type_id;
    }
}

bool equal(Item a, Item b) {
    printf("equal expr\n");
    LambdaItem a_item = {.item = a};
    LambdaItem b_item = {.item = b};
    if (a_item.type_id != b_item.type_id) {
        // number promotion
        if (LMD_TYPE_IMP_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_IMP_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item.item);
            double b_val = it2d(b_item.item);
            return a_val == b_val;
        }
        return false;
    }
    if (a_item.type_id == LMD_TYPE_NULL) {
        return true;
    }    
    else if (a_item.type_id == LMD_TYPE_IMP_INT) {
        return a_item.long_val == b_item.long_val;
    }
    else if (a_item.type_id == LMD_TYPE_INT) {
        return *(long*)a_item.pointer == *(long*)b_item.pointer;
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT) {
        return *(double*)a_item.pointer == *(double*)b_item.pointer;
    }
    else if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL || 
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
        return str_a->len == str_b->len && strncmp(str_a->chars, str_b->chars, str_a->len) == 0;
    }
    printf("unknown comparing type %d\n", a_item.type_id);
    return false;
}

bool in(Item a, Item b) {
    printf("in expr\n");
    LambdaItem a_item = {.item = a};
    LambdaItem b_item = {.item = b};
    if (b_item.type_id) { // b is scalar
        if (b_item.type_id == LMD_TYPE_STRING && a_item.type_id == LMD_TYPE_STRING) {
            String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
    }
    else { // b is container
        TypeId b_type = *((uint8_t*)b);
        if (b_type == LMD_TYPE_LIST) {
            List *list = (List*)b;
            for (int i = 0; i < list->length; i++) {
                if (equal(list->items[i], a)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY) {
            Array *arr = (Array*)b;
            for (int i = 0; i < arr->length; i++) {
                if (equal(arr->items[i], a)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT) {
            ArrayLong *arr = (ArrayLong*)b;
            for (int i = 0; i < arr->length; i++) {
                if (arr->items[i] == it2l(a)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_MAP) {
            // check if a is in map
        }
        else if (b_type == LMD_TYPE_ELEMENT) {
            // check if a is in element
        }
        else {
            printf("invalid type %d\n", b_type);
        }
    }
    return false;
}

String STR_NULL = {.chars = "null", .len = 4};
String STR_TRUE = {.chars = "true", .len = 4};
String STR_FALSE = {.chars = "false", .len = 5};

String* string(Context *rt, Item item) {
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_NULL) {
        return &STR_NULL;
    }
    else if (itm.type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    }    
    else if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_BINARY || itm.type_id == LMD_TYPE_DTIME) {
        return (String*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_IMP_INT) {
        char buf[32];
        int int_val = (int32_t)itm.long_val;
        snprintf(buf, sizeof(buf), "%d", int_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        char buf[32];
        long long_val = *(long*)itm.pointer;
        snprintf(buf, sizeof(buf), "%ld", long_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        char buf[32];
        double dval = *(double*)itm.pointer;
        snprintf(buf, sizeof(buf), "%g", dval);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    printf("unhandled type %d\n", itm.type_id);
    return NULL;
}

LambdaType* type(Context *rt, Item item) {
    LambdaItem itm = {.item = item};
    LambdaTypeType *type = calloc(1, sizeof(LambdaTypeType) + sizeof(LambdaType)); 
    LambdaType *item_type = (LambdaType *)((uint8_t *)type + sizeof(LambdaTypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (itm.type_id) {
        item_type->type_id = itm.type_id;
    }
    else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        item_type->type_id = *((uint8_t*)item);
    }
    return (LambdaType*)type;
}