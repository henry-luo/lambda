#pragma once

// Enhanced GMP configuration for cross-compilation
#ifdef CROSS_COMPILE
    // For cross-compilation, we may have either full or stub GMP
    #include <gmp.h>
    
    // Declare weak symbols for GMP I/O functions to detect availability at runtime
    extern int gmp_sprintf(char *, const char *, ...) __attribute__((weak));
    extern double mpf_get_d(const mpf_t) __attribute__((weak));
    
    // Helper macro to check if full GMP I/O is available
    #define HAS_GMP_IO() (gmp_sprintf != NULL)
#else
    // Native compilation should have full GMP
    #include <gmp.h>
    #define HAS_GMP_IO() 1
#endif

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

#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"
#include "../lib/num_stack.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"

/*
# Lambda Runtime Data Structures

Lambda runtime uses the following to represent its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DECIMAL, LMD_TYPE_DTIME, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct/raw pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
- can use get_type_id() function to get the TypeId of an Item in a general manner;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not as Map directly;
*/

#include "lambda.h"

#pragma clang diagnostic pop

#ifdef __cplusplus
}
#endif

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

typedef struct TypeDecimal : TypeConst  {
    mpf_t dec_val;
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
} Operator;

typedef enum SysFunc {
    SYSFUNC_LEN,
    SYSFUNC_TYPE,
    SYSFUNC_INT,
    SYSFUNC_FLOAT,
    SYSFUNC_NUMBER,
    SYSFUNC_STRING,
    SYSFUNC_CHAR,
    SYSFUNC_SYMBOL,
    SYSFUNC_DATETIME,
    SYSFUNC_DATE,
    SYSFUNC_TIME,
    SYSFUNC_TODAY,
    SYSFUNC_JUSTNOW,
    SYSFUNC_SET,
    SYSFUNC_SLICE,    
    SYSFUNC_ALL,
    SYSFUNC_ANY,
    SYSFUNC_MIN,
    SYSFUNC_MAX,
    SYSFUNC_SUM,
    SYSFUNC_AVG,
    SYSFUNC_ABS,
    SYSFUNC_ROUND,
    SYSFUNC_FLOOR,
    SYSFUNC_CEIL,
    SYSFUNC_INPUT,
    SYSFUNC_PRINT,
    SYSFUNC_FORMAT,
    SYSFUNC_ERROR,
} SysFunc;

typedef struct TypeBinary : Type {
    Type* left;
    Type* right;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
} TypeBinary;

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
    return value.type_id ? value.type_id : *((TypeId*)value.raw_pointer);
}

extern String EMPTY_STRING;
String* strbuf_to_string(StrBuf *sb);

typedef struct Input {
    void* url;
    void* path;
    VariableMemPool* pool; // memory pool
    ArrayList* type_list;  // list of types
    Item root;
    StrBuf* sb;
} Input;

Array* array_pooled(VariableMemPool *pool);
void array_append(Array* arr, Item itm, VariableMemPool *pool);
Map* map_pooled(VariableMemPool *pool);
Element* elmt_pooled(VariableMemPool *pool);
void elmt_put(Element* elmt, String* key, Item value, VariableMemPool* pool);

Type* alloc_type(VariableMemPool* pool, TypeId type, size_t size);



