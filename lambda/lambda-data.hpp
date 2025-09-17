#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>  // for cross-platform integer formatting
#include <math.h>
#include <mpdecimal.h>

typedef struct NamePool NamePool;

#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"
#include "../lib/num_stack.h"
#include "../lib/datetime.h"
#include "../lib/url.h"

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// Avoid conflicts with C++ headers by undefining after including lambda.h
#include "lambda.h"
#undef max
#undef min

#ifdef __cplusplus
}
#endif

// get raw value out of an Item
inline double get_double(Item item) { return *(double*)item.pointer; }
inline int64_t get_int64(Item item) { return *(int64_t*)item.pointer; }
inline DateTime get_datetime(Item item) { return *(DateTime*)item.pointer; }
inline Decimal* get_decimal(Item item) { return (Decimal*)item.pointer; }
inline String* get_string(Item item) { return (String*)item.pointer; }
inline String* get_symbol(Item item) { return (String*)item.pointer; }
inline String* get_binary(Item item) { return (String*)item.pointer; }

// Unicode-enhanced comparison functions are declared in utf_string.h
#include "utf_string.h"

typedef struct TypeInfo {
    int byte_size;  // byte size of the type
    char* name;  // name of the type
    Type *type;  // literal type
    Type *lit_type;  // literal type_type
    // char* c_type;  // C type of the type
} TypeInfo;

extern TypeInfo type_info[];

// const_index, type_index - 32-bit, there should not be more than 4G types and consts in a single Lambda runtime
// list item count, map size - 64-bit, to support large data files

// mapping from data to its owner
typedef struct DataOwner {
    void *data;
    void *owner;  // element/map/list/array that contains/owns the data
} DataOwner;

struct Decimal {
    uint16_t ref_cnt;
    mpd_t* dec_val;  // libmpdec decimal number
};

#pragma pack(push, 1)
typedef struct TypedItem {
    TypeId type_id;
    union {
        // inline value types
        bool bool_val;
        int int_val;
        long long_val;
        float float_val;
        double double_val;
        DateTime datetime_val;

        // pointer types
        Decimal* decimal;
        String* string;
        Range* range;
        Array* array;
        List* list;
        Map* map;
        Element* element;
        void* pointer;
    };
} TypedItem;
#pragma pack(pop)

struct Map : Container {
    void* type;  // map type/shape
    void* data;  // packed data struct of the map
    int data_cap;  // capacity of the data struct
};

struct Element : List {
    // attributes map
    void* type;  // attr type/shape
    void* data;  // packed data struct of the attrs
    int data_cap;  // capacity of the data struct
};

typedef struct Script Script;

typedef struct : Type {
    int const_index;
} TypeConst;

typedef struct TypeFloat : TypeConst {
    double double_val;
} TypeFloat;

typedef struct TypeInt64 : TypeConst {
    int64_t int64_val;
} TypeInt64;

typedef struct TypeDateTime : TypeConst {
    DateTime datetime;
} TypeDateTime;

typedef struct TypeDecimal : TypeConst  {
    Decimal *decimal;
} TypeDecimal;

typedef struct TypeString : TypeConst {
    String *string;
} TypeString;

typedef TypeString TypeSymbol;

typedef struct TypeArray : Type {
    Type* nested;  // nested item type for the array
    long length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
} TypeArray;

typedef TypeArray TypeList;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;  // type of the field
    long byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
} ShapeEntry;

typedef struct TypeMap : Type {
    long length;  // no. of items in the map
    long byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    ShapeEntry* shape;  // first shape entry of the map
    ShapeEntry* last;  // last shape entry of the map
} TypeMap;

typedef struct TypeElmt : TypeMap {
    StrView name;  // name of the element
    long content_length;  // no. of content items, needed for element type
} TypeElmt;

typedef enum Operator {
    // unary
    OPERATOR_NOT,
    OPERATOR_NEG,
    OPERATOR_POS,

    // binary
    OPERATOR_ADD,
    OPERATOR_SUB,
    OPERATOR_MUL,
    OPERATOR_POW,
    OPERATOR_DIV,
    OPERATOR_IDIV,
    OPERATOR_MOD,

    OPERATOR_AND,
    OPERATOR_OR,

    OPERATOR_EQ,
    OPERATOR_NE,
    OPERATOR_LT,
    OPERATOR_LE,
    OPERATOR_GT,
    OPERATOR_GE,

    OPERATOR_TO,
    OPERATOR_UNION,
    OPERATOR_INTERSECT,
    OPERATOR_EXCLUDE,
    OPERATOR_IS,
    OPERATOR_IN,

    // occurrence
    OPERATOR_OPTIONAL,  // ?
    OPERATOR_ONE_MORE,  // +
    OPERATOR_ZERO_MORE,  // *
} Operator;

typedef enum SysFunc {
    SYSFUNC_LEN,
    SYSFUNC_TYPE,
    SYSFUNC_INT,
    SYSFUNC_INT64,
    SYSFUNC_FLOAT,
    SYSFUNC_DECIMAL,
    SYSFUNC_NUMBER,
    SYSFUNC_STRING,
    //SYSFUNC_CHAR,
    SYSFUNC_SYMBOL,
    SYSFUNC_BINARY,
    SYSFUNC_DATETIME,
    SYSFUNC_DATE,
    SYSFUNC_TIME,
    SYSFUNC_JUSTNOW,
    SYSFUNC_SET,
    SYSFUNC_SLICE,    
    SYSFUNC_ALL,
    SYSFUNC_ANY,
    SYSFUNC_MIN1,
    SYSFUNC_MIN2,
    SYSFUNC_MAX1,
    SYSFUNC_MAX2,
    SYSFUNC_SUM,
    SYSFUNC_AVG,
    SYSFUNC_ABS,
    SYSFUNC_ROUND,
    SYSFUNC_FLOOR,
    SYSFUNC_CEIL,
    SYSFUNC_INPUT1,
    SYSFUNC_INPUT2,
    SYSFUNC_FORMAT1,
    SYSFUNC_FORMAT2,
    SYSFUNC_ERROR,
    SYSFUNC_NORMALIZE,
    // procedural functions
    SYSPROC_NOW,
    SYSPROC_TODAY,
    SYSPROC_PRINT,
    SYSPROC_FETCH,
    SYSPROC_OUTPUT,
    SYSPROC_CMD,
} SysFunc;

typedef struct TypeBinary : Type {
    Type* left;
    Type* right;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
} TypeBinary;

typedef struct TypeUnary : Type {
    Type* operand;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
} TypeUnary;

typedef struct TypeParam : Type {
    struct TypeParam *next;
} TypeParam;

typedef struct TypeFunc : Type {
    TypeParam *param;
    Type *returned;
    int param_count;
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
} TypeFunc;

typedef struct TypeSysFunc : Type {
    SysFunc *fn;
} TypeSysFunc;

typedef struct TypeType : Type {
    Type *type;  // full type defintion
} TypeType;

struct Pack {
    size_t size;           // Current used size of the pack
    size_t capacity;       // Total capacity of the pack
    size_t committed_size; // Currently committed memory size - non-zero indicates virtual memory mode
    void* data;            // Pointer to the allocated memory
};
Pack* pack_init(size_t initial_size);
void* pack_alloc(Pack* pack, size_t size);
void* pack_calloc(Pack* pack, size_t size);
void pack_free(Pack* pack);

// get type_id from an Item
static inline TypeId get_type_id(Item value) {
    if (value.type_id) {
        return value.type_id;
    }
    if (value.raw_pointer) {
        return *((TypeId*)value.raw_pointer);
    }
    return LMD_TYPE_NULL; // fallback for null items
}

extern Type TYPE_NULL;
extern Type TYPE_BOOL;
extern Type TYPE_INT;
extern Type TYPE_INT64;
extern Type TYPE_FLOAT;
extern Type TYPE_DECIMAL;
extern Type TYPE_NUMBER;
extern Type TYPE_STRING;
extern Type TYPE_BINARY;
extern Type TYPE_SYMBOL;
extern Type TYPE_DTIME;
extern Type TYPE_LIST;
extern Type TYPE_RANGE;
extern TypeArray TYPE_ARRAY;
extern Type TYPE_MAP;
extern Type TYPE_ELMT;
extern Type TYPE_TYPE;
extern Type TYPE_FUNC;
extern Type TYPE_ANY;
extern Type TYPE_ERROR;

extern Type CONST_BOOL;
extern Type CONST_INT;
extern Type CONST_FLOAT;
extern Type CONST_STRING;

extern Type LIT_NULL;
extern Type LIT_BOOL;
extern Type LIT_INT;
extern Type LIT_INT64;
extern Type LIT_FLOAT;
extern Type LIT_DECIMAL;
extern Type LIT_STRING;
extern Type LIT_DTIME;
extern Type LIT_TYPE;

extern TypeType LIT_TYPE_NULL;
extern TypeType LIT_TYPE_BOOL;
extern TypeType LIT_TYPE_INT;
extern TypeType LIT_TYPE_FLOAT;
extern TypeType LIT_TYPE_DECIMAL;
extern TypeType LIT_TYPE_NUMBER;
extern TypeType LIT_TYPE_STRING;
extern TypeType LIT_TYPE_BINARY;
extern TypeType LIT_TYPE_SYMBOL;
extern TypeType LIT_TYPE_DTIME;
extern TypeType LIT_TYPE_LIST;
extern TypeType LIT_TYPE_RANGE;
extern TypeType LIT_TYPE_ARRAY;
extern TypeType LIT_TYPE_MAP;
extern TypeType LIT_TYPE_ELMT;
extern TypeType LIT_TYPE_FUNC;
extern TypeType LIT_TYPE_TYPE;
extern TypeType LIT_TYPE_ANY;
extern TypeType LIT_TYPE_ERROR;

extern TypeMap EmptyMap;
extern TypeElmt EmptyElmt;
extern Item ItemNull;
extern Item ItemError;
extern String EMPTY_STRING;
extern TypeInfo type_info[];

typedef struct Input {
    void* url;
    void* path;
    VariableMemPool* pool;      // memory pool
    NamePool* name_pool;        // centralized name management
    ArrayList* type_list;       // list of types
    Item root;
    StringBuf* sb;
} Input;

Array* array_pooled(VariableMemPool *pool);
void array_append(Array* arr, Item itm, VariableMemPool *pool);
Map* map_pooled(VariableMemPool *pool);
TypedItem map_get_typed(Map* map, Item key);
TypedItem list_get_typed(List* list, int index);
Element* elmt_pooled(VariableMemPool *pool);
TypedItem elmt_get_typed(Element* elmt, Item key);
void elmt_put(Element* elmt, String* key, Item value, VariableMemPool* pool);

Type* alloc_type(VariableMemPool* pool, TypeId type, size_t size);



