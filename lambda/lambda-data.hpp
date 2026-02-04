#pragma once

#include <string.h>  // moved outside extern "C" block to fix C++ compatibility

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cstdint>  // C++
#include <inttypes.h>  // for cross-platform integer formatting
#include <math.h>

// Forward declaration for mpdecimal types (full definition in lambda-decimal.cpp)
typedef struct mpd_context_t mpd_context_t;
typedef struct mpd_t mpd_t;

#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
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

// Forward declarations for C++ types
class SchemaValidator;

#include "lambda.hpp"
#undef max
#undef min

#include "name_pool.hpp"
#include "shape_pool.hpp"

// void *memcpy(void *dest, const void *src, size_t n);
// void *memset(void *s, int c, size_t n);
// int memcmp(const void *s1, const void *s2, size_t n);
// size_t strlen(const char *s);
// int strcmp(const char *s1, const char *s2);
// int strncmp(const char *s1, const char *s2, size_t n);
// int strcasecmp(const char *s1, const char *s2);
// int strncasecmp(const char *s1, const char *s2, size_t n);
// char *strchr(const char *s, int c);
// char *strcpy(char *dest, const char *src);
// char *strncpy(char *dest, const char *src, size_t n);
// char *strdup(const char *s);
// char *strstr(const char *target, const char *source);
// char *strrchr(const char *s, int c);
// char *strtok(char *str, const char *delim);

#ifdef __cplusplus
}
#endif


typedef struct Heap Heap;
typedef struct Pack Pack;
typedef struct mpd_context_t mpd_context_t;
typedef struct num_stack_t num_stack_t;
struct LambdaError;  // forward declaration

typedef struct EvalContext : Context {
    Heap* heap;
    Pool* ast_pool;
    NamePool* name_pool;        // name_pool for runtime-generated names
    ArrayList* type_list;
    num_stack_t* num_stack;  // for long and double pointers
    void* type_info;  // meta info for the base types
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
    SchemaValidator* validator; // Schema validator for document validation

    // Error handling and stack trace support
    ArrayList* debug_info;      // function address → source mapping for stack traces
    const char* current_file;   // current source file (for error reporting)
    LambdaError* last_error;    // most recent runtime error (owned)
} EvalContext;

// Unicode-enhanced comparison functions are declared in utf_string.h
#include "utf_string.h"

typedef struct TypeInfo {
    int byte_size;  // byte size of the type
    char* name;  // name of the type
    Type* type;  // literal type
    Type* lit_type;  // literal type_type
    // char* c_type;  // C type of the type
} TypeInfo;

extern TypeInfo type_info[];

// const_index, type_index - 32-bit, there should not be more than 4G types and consts in a single Lambda runtime
// list item count, map size - 64-bit, to support large data files

typedef struct mpd_t mpd_t;
struct Decimal {
    uint16_t ref_cnt;
    mpd_t* dec_val;  // libmpdec decimal number
};

#pragma pack(push, 1)
// TypedItem for storing data in map with type_id
typedef struct TypedItem {
    TypeId type_id;
    union {
        // inline value types
        bool bool_val;
        int int_val;
        int64_t long_val;
        // float float_val;
        double double_val;
        DateTime datetime_val;
        uint64_t item;

        // pointer types
        void* pointer;
        Decimal* decimal;
        String* string;

        // containers
        Container* container;
        Range* range;
        Array* array;
        List* list;
        Map* map;
        Element* element;
        Type* type;
        Function* function;
        Path* path;
    };
} TypedItem;
#pragma pack(pop)

typedef struct Script Script;

typedef struct TypeConst : Type {
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

typedef struct TypeDecimal : TypeConst {
    Decimal* decimal;
} TypeDecimal;

typedef struct TypeString : TypeConst {
    String* string;
} TypeString;

typedef TypeString TypeSymbol;

typedef struct TypeArray : Type {
    Type* nested;  // nested item type for the array
    int64_t length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
} TypeArray;

typedef TypeArray TypeList;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;  // type of the field
    int64_t byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
} ShapeEntry;

typedef struct TypeMap : Type {
    int64_t length;  // no. of items in the map
    int64_t byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    ShapeEntry* shape;  // first shape entry of the map
    ShapeEntry* last;  // last shape entry of the map
} TypeMap;

typedef struct TypeElmt : TypeMap {
    StrView name;  // name of the element
    int64_t content_length;  // no. of content items, needed for element type
} TypeElmt;

typedef enum Operator {
    // unary
    OPERATOR_NOT,
    OPERATOR_NEG,
    OPERATOR_POS,

    // binary
    OPERATOR_ADD,
    OPERATOR_JOIN,
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

    // pipe operators
    OPERATOR_PIPE,      // | pipe operator
    OPERATOR_WHERE,     // where filter clause

    // occurrence
    OPERATOR_OPTIONAL,  // ?
    OPERATOR_ONE_MORE,  // +
    OPERATOR_ZERO_MORE,  // *
    OPERATOR_REPEAT,    // {n}, {n,}, {n,m} for patterns
} Operator;

// Character class types for pattern matching
typedef enum PatternCharClass {
    PATTERN_DIGIT,      // \d - [0-9]
    PATTERN_WORD,       // \w - [a-zA-Z0-9_]
    PATTERN_SPACE,      // \s - whitespace
    PATTERN_ALPHA,      // \a - [a-zA-Z]
    PATTERN_ANY,        // \. - any character
} PatternCharClass;

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
    SYSFUNC_NORMALIZE2,     // normalize(str, form) with 2 args
    // string functions
    SYSFUNC_CONTAINS,
    SYSFUNC_STARTS_WITH,
    SYSFUNC_ENDS_WITH,
    SYSFUNC_INDEX_OF,
    SYSFUNC_LAST_INDEX_OF,
    SYSFUNC_TRIM,
    SYSFUNC_TRIM_START,
    SYSFUNC_TRIM_END,
    SYSFUNC_SPLIT,
    SYSFUNC_STR_JOIN,       // join(strs, sep) for strings
    SYSFUNC_REPLACE,
    // vector functions
    SYSFUNC_PROD,
    SYSFUNC_CUMSUM,
    SYSFUNC_CUMPROD,
    SYSFUNC_ARGMIN,
    SYSFUNC_ARGMAX,
    SYSFUNC_FILL,
    SYSFUNC_DOT,
    SYSFUNC_NORM,
    // statistical functions
    SYSFUNC_MEAN,
    SYSFUNC_MEDIAN,
    SYSFUNC_VARIANCE,
    SYSFUNC_DEVIATION,
    // element-wise math functions
    SYSFUNC_SQRT,
    SYSFUNC_LOG,
    SYSFUNC_LOG10,
    SYSFUNC_EXP,
    SYSFUNC_SIN,
    SYSFUNC_COS,
    SYSFUNC_TAN,
    SYSFUNC_SIGN,
    // vector manipulation functions
    SYSFUNC_REVERSE,
    SYSFUNC_SORT,
    SYSFUNC_SORT2,
    SYSFUNC_UNIQUE,
    SYSFUNC_CONCAT,
    SYSFUNC_TAKE,
    SYSFUNC_DROP,
    SYSFUNC_ZIP,
    SYSFUNC_RANGE3,
    SYSFUNC_QUANTILE,
    // variadic parameter access
    SYSFUNC_VARG0,          // varg() - get all variadic args as list
    SYSFUNC_VARG1,          // varg(n) - get nth variadic arg
    // procedural functions
    SYSPROC_NOW,
    SYSPROC_TODAY,
    SYSPROC_PRINT,
    SYSPROC_FETCH,
    SYSPROC_OUTPUT2,         // output(source, url) - auto-detect format
    SYSPROC_OUTPUT3,         // output(source, url, format) - explicit format
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
    int min_count;   // minimum occurrence count (for OPERATOR_REPEAT)
    int max_count;   // maximum occurrence count (-1 for unbounded)
} TypeUnary;

typedef struct TypeParam : Type {
    struct TypeParam* next;
    bool is_optional;           // whether parameter is optional (? marker or default value)
    struct AstNode* default_value;  // default value expression (NULL if none)
} TypeParam;

typedef struct TypeFunc : Type {
    TypeParam* param;
    Type* returned;
    int param_count;
    int required_param_count;   // count of required (non-optional) parameters
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
    bool is_variadic;           // function accepts variadic args (...)
} TypeFunc;

typedef struct TypeSysFunc : Type {
    SysFunc* fn;
} TypeSysFunc;

typedef struct TypeType : Type {
    Type* type;  // full type defintion
} TypeType;

// Forward declaration for RE2
namespace re2 { class RE2; }

// Compiled string/symbol pattern for regex matching
typedef struct TypePattern : Type {
    int pattern_index;      // index in type_list for runtime access
    bool is_symbol;         // true for symbol pattern, false for string pattern
    re2::RE2* re2;          // compiled RE2 regex (owned)
    String* source;         // original pattern source for error messages
} TypePattern;

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

extern Type TYPE_NULL;
extern Type TYPE_UNDEFINED;  // JavaScript undefined
extern Type TYPE_BOOL;
extern Type TYPE_INT;
extern Type TYPE_INT64;
extern Type TYPE_FLOAT;
extern Type TYPE_DECIMAL;
extern Type TYPE_NUMBER;
extern Type TYPE_STRING;
extern Type TYPE_BINARY;
extern Type TYPE_SYMBOL;
extern Type TYPE_PATH;
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
extern TypeType LIT_TYPE_PATH;
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
extern TypeInfo type_info[];

typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    ShapePool* shape_pool;      // shape deduplication (NEW)
    ArrayList* type_list;       // list of types
    Item root;
    Input* parent;              // parent Input for hierarchical ownership (nullable)
    char* xml_stylesheet_href;  // href from <?xml-stylesheet?> processing instruction (nullable)
    // StringBuf* sb;

    // member functions
    static Input* create(Pool* pool, Url* abs_url = nullptr, Input* parent = nullptr);
} Input;

#ifdef __cplusplus
extern "C" {
#endif

// Pool-based allocation (for runtime)
Array* array_pooled(Pool* pool);
void array_append(Array* arr, Item itm, Pool* pool, Arena* arena = nullptr);
Map* map_pooled(Pool* pool);
Element* elmt_pooled(Pool* pool);

// Arena-based allocation (for MarkBuilder)
Array* array_arena(Arena* arena);
Map* map_arena(Arena* arena);
Element* elmt_arena(Arena* arena);
List* list_arena(Arena* arena);

void map_put(Map* mp, String* key, Item value, Input *input);
void elmt_put(Element* elmt, String* key, Item value, Pool* pool);

// Shape finalization - deduplicate map/element shapes using shape pool
void map_finalize_shape(TypeMap* type_map, Input* input);
void elmt_finalize_shape(TypeElmt* type_elmt, Input* input);

#ifdef __cplusplus
}
#endif

Type* alloc_type(Pool* pool, TypeId type, size_t size);
