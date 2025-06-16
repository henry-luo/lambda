#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <tree_sitter/api.h>
#include <gmp.h>
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"

#include "ts-enum.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define SYM_NULL sym_null
#define SYM_TRUE sym_true
#define SYM_FALSE sym_false
#define SYM_INF sym_inf
#define SYM_NAN sym_nan
#define SYM_INT sym_integer
#define SYM_FLOAT sym_float
#define SYM_DECIMAL sym_decimal
#define SYM_STRING sym_string
#define SYM_SYMBOL sym_symbol
#define SYM_DATETIME sym_datetime
#define SYM_TIME sym_time
#define SYM_BINARY sym_binary

#define SYM_CONTENT sym_content
#define SYM_LIST sym_list
#define SYM_ARRAY sym_array
#define SYM_MAP sym_map
#define SYM_ELEMENT sym_element
#define SYM_ATTR sym_attr

#define SYM_IDENT sym_identifier
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_SUBSCRIPT_EXPR sym_subscript_expr
#define SYM_CALL_EXPR sym_call_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_UNARY_EXPR sym_unary_expr
#define SYM_BINARY_EXPR sym_binary_expr

#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_IF_EXPR sym_if_expr
#define SYM_IF_STAM sym_if_stam
#define SYM_LET_STAM sym_let_stam
#define SYM_FOR_EXPR sym_for_expr
#define SYM_FOR_STAM sym_for_stam

#define SYM_BASE_TYPE sym_base_type
#define SYM_TYPE_DEFINE sym_type_definition
#define SYM_TYPE_ANNOTE sym_type_annotation

#define SYM_FUNC_STAM sym_fn_stam
#define SYM_FUNC_EXPR_STAM sym_fn_expr_stam
#define SYM_FUNC_EXPR sym_fn_expr
#define SYM_SYS_FUNC sym_sys_func
#define SYM_IMPORT_MODULE sym_import_module

#define SYM_COMMENT sym_comment

#define FIELD_COND field_cond
#define FIELD_THEN field_then
#define FIELD_ELSE field_else
#define FIELD_LEFT field_left
#define FIELD_RIGHT field_right
#define FIELD_NAME field_name
#define FIELD_AS field_as
#define FIELD_TYPE field_type
#define FIELD_OBJECT field_object
#define FIELD_FIELD field_field
#define FIELD_BODY field_body
#define FIELD_DECLARE field_declare
#define FIELD_FUNCTION field_function
#define FIELD_ARGUMENT field_argument
#define FIELD_OPERATOR field_operator
#define FIELD_OPERAND field_operand
#define FIELD_ALIAS field_alias
#define FIELD_MODULE field_module
#define FIELD_PUB field_pub

typedef enum {
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
} Operator ;

typedef enum {
    SYSFUNC_LENGTH,
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
    SYSFUNC_PRINT,
    SYSFUNC_ERROR,
} SysFunc;

typedef struct TypeInfo {
    int byte_size;  // byte size of the type
    char* name;  // name of the type
    // char* c_type;  // C type of the type
} TypeInfo;

extern TypeInfo type_info[];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"

#include "lambda.h"

// const_index, type_index - 32-bit, there should not be more than 4G types and consts in a single Lambda runtime
// list item count, map size - 64-bit, to support large data files

// mapping from data to its owner
typedef struct DataOwner {
    void *data;
    void *owner;  // element/map/list/array that contains/owns the data
} DataOwner;

struct Map {
    Container;  // extends Container
    void* type;  // map type/shape
    void* data;  // packed data struct of the map
};

struct Element {
    List;  // extends List for content
    // attributes
    void* type;  // attr type/shape
    void* data;  // packed data struct of the attrs
};

typedef struct Script Script;

typedef struct {
    LambdaType;  // extends LambdaType
    int const_index;
} LambdaTypeConst;

typedef struct {
    LambdaTypeConst;  // extends LambdaTypeConst
    double double_val;
} LambdaTypeFloat;

typedef struct {
    LambdaTypeConst;  // extends LambdaTypeConst
    mpf_t dec_val;
} LambdaTypeDecimal;

typedef struct {
    LambdaTypeConst;  // extends LambdaTypeConst
    String *string;
} LambdaTypeString;

typedef LambdaTypeString LambdaTypeSymbol;

typedef struct {
    LambdaType;  // extends LambdaType
    LambdaType* nested;  // nested item type for the array
    long length;  // no. of items in the array/map
} LambdaTypeArray;

typedef LambdaTypeArray LambdaTypeList;

typedef struct ShapeEntry {
    StrView name;
    LambdaType* type;  // type of the field
    long byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
} ShapeEntry;

typedef struct {
    LambdaType;  // extends LambdaType
    long length;  // no. of items in the map
    long byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    ShapeEntry* shape;  // shape of the map
} LambdaTypeMap;

typedef struct {
    LambdaTypeMap; // extends LambdaTypeMap
    StrView name;  // name of the element
    long content_length;  // no. of content items
} LambdaTypeElmt;

typedef struct LambdaTypeParam {
    LambdaType;  // extends LambdaType
    struct LambdaTypeParam *next;
} LambdaTypeParam;

typedef struct {
    LambdaType;  // extends LambdaType
    LambdaTypeParam *param;
    LambdaType *returned;
    int param_count;
    bool is_anonymous;
    bool is_public;
} LambdaTypeFunc;

typedef struct {
    LambdaType;
    SysFunc *fn;
} LambdaTypeSysFunc;

typedef struct {
    LambdaType;  // extends LambdaType
    LambdaType *type;
} LambdaTypeType;

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

typedef struct AstNode AstNode;

// entry in the name_stack
typedef struct NameEntry {
    StrView name;
    AstNode* node;  // AST node that defines the name
    struct NameEntry* next;
    bool is_imported;
} NameEntry;

// name_scope
typedef struct NameScope {
    NameEntry* first;  // start name entry in the current scope
    NameEntry* last;  // last name entry in the current scope
    struct NameScope* parent;  // parent scope
} NameScope;

typedef enum AstNodeType {
    AST_NODE_NULL,
    AST_NODE_PRIMARY,
    AST_NODE_UNARY,
    AST_NODE_BINARY,
    AST_NODE_LIST,
    AST_NODE_CONTENT,
    AST_NODE_ARRAY,
    AST_NODE_MAP,
    AST_NODE_ELEMENT,
    AST_NODE_KEY_EXPR,
    AST_NODE_ASSIGN,
    AST_NODE_LOOP,
    AST_NODE_IF_EXPR,
    AST_NODE_FOR_EXPR,
    AST_NODE_LET_STAM,
    AST_NODE_FIELD_EXPR,
    AST_NODE_CALL_EXPR,
    AST_NODE_SYS_FUNC,
    AST_NODE_IDENT,
    AST_NODE_PARAM,
    AST_NODE_TYPE,
    AST_NODE_FUNC,
    AST_NODE_FUNC_EXPR,
    AST_NODE_IMPORT,
    AST_SCRIPT,
} AstNodeType;

struct AstNode {
    AstNodeType node_type;
    LambdaType *type;
    struct AstNode* next;
    TSNode node;
};

typedef struct {
    AstNode;  // extends AstNode
    AstNode *object, *field;
} AstFieldNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *function;
    AstNode *argument;
} AstCallNode;

typedef struct {
    AstNode;  // extends AstNode
    SysFunc fn;
} AstSysFuncNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *expr;
} AstPrimaryNode;

typedef AstNode AstTypeNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *operand;
    StrView operator;
    Operator op;
} AstUnaryNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *left, *right;
    StrView operator;
    Operator op;
} AstBinaryNode;

typedef struct {
    AstNode;  // extends AstNode
    StrView name;
    AstNode *as;
} AstNamedNode;

typedef struct {
    AstNode;  // extends AstNode
    StrView alias;
    StrView module;
    Script* script; // imported script
    bool is_relative;
} AstImportNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *declare;  // declarations in let expression
} AstLetNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *loop;
    AstNode *then;
    NameScope *vars;  // scope for the variables in the loop
} AstForNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *cond;
    AstNode *then;
    AstNode *otherwise;
} AstIfExprNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *item;  // first item in the array
} AstArrayNode;

typedef struct {
    AstArrayNode;  // extends AstArrayNode
    AstNode *declare;  // declarations in the list
    NameScope *vars;  // scope for the variables in the list
} AstListNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNamedNode *item;  // first item in the map
} AstMapNode;

typedef struct {
    AstMapNode;  // extends AstMapNode
    AstNode *content;  // first content node
} AstElementNode;

// aligned with AstNamedNode
typedef struct {
    AstNode;  // extends AstNode
    StrView name;
    AstNode *body;
    AstNamedNode *param; // first parameter of the function
    NameScope *vars;  // vars including params and local variables
} AstFuncNode;

// root of the AST
typedef struct {
    AstNode;  // extends AstNode
    AstNode *child;  // first child
    NameScope *global_vars;  // global variables
} AstScript;

typedef struct Heap {
    VariableMemPool *pool;  // memory pool for the heap
    // HeapEntry *first, *last;  // first and last heap entry
    ArrayList *entries;  // list of allocation entries
} Heap;

void heap_init();
void* heap_alloc(size_t size, TypeId type_id);
void* heap_calloc(size_t size, TypeId type_id);
void heap_destroy();
void frame_start();
void frame_end();
void free_item(Item item, bool clear_entry);

// uses the high byte to tag the pointer, defined for little-endian
typedef union LambdaItem {
    struct {
        union {
            struct {
                uint64_t long_val: 56;
                uint64_t _8: 8;
            };
            struct {
                uint64_t bool_val: 8;
                uint64_t _56: 56;
            };
            struct {
                uint64_t pointer : 56;  // tagged pointer for long, double, string, symbol, dtime, binary
                uint64_t type_id : 8;        
            };           
        };
    };
    uint64_t item;
    void* raw_pointer;
} LambdaItem;

#include <mir.h>
#include <mir-gen.h>
#include <c2mir.h>

typedef Item (*main_func_t)(Context*);
typedef struct Runtime Runtime;

struct Script {
    const char* reference;  // path (relative to the main script) and name of the script
    int index;  // index of the script in the runtime scripts list
    const char* source;
    TSTree* syntax_tree;

    // AST
    VariableMemPool* ast_pool;
    AstNode *ast_root;
    // todo: have a hashmap to speed up name lookup
    NameScope* current_scope;  // current name scope
    ArrayList* type_list;  // list of types
    ArrayList* const_list;  // list of constants

    // each script is JIT compiled its own MIR context
    MIR_context_t jit_context;
    main_func_t main_func;  // transpiled main function
};

typedef struct Transpiler {
    Script;  // extends Script
    TSParser* parser;
    StrBuf* code_buf;
    Runtime* runtime;
} Transpiler;

typedef struct Runner {
    Script* script;
    Context context;  // execution context
} Runner;

struct Runtime {
    ArrayList* scripts;  // list of (loaded) scripts
    TSParser* parser;
    char* current_dir;
};

#define ts_node_source(transpiler, node)  {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

void* alloc_ast_bytes(Transpiler* tp, size_t size);
void* alloc_const(Transpiler* tp, size_t size);
LambdaType* alloc_type(Transpiler* tp, TypeId type, size_t size);
AstNode* build_map(Transpiler* tp, TSNode map_node);
AstNode* build_element(Transpiler* tp, TSNode element_node);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global);
AstNode* build_script(Transpiler* tp, TSNode script_node);
void print_ast_node(AstNode *node, int indent);
void print_ts_node(const char *source, TSNode node, uint32_t indent);
void writeNodeSource(Transpiler* tp, TSNode node);
void writeType(Transpiler* tp, LambdaType *type);
void push_name(Transpiler* tp, AstNamedNode* node, bool is_imported);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node);

MIR_context_t jit_init();
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void jit_cleanup(MIR_context_t ctx);

typedef uint64_t Item;

Script* load_script(Runtime *runtime, const char* script_path, const char* source);
void runner_init(Runtime *runtime, Runner* runner);
void runner_cleanup(Runner* runner);
Item run_script(Runtime *runtime, const char* source, char* script_path);
Item run_script_at(Runtime *runtime, char* script_path);
void print_item(StrBuf *strbuf, Item item);

void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);

#pragma clang diagnostic pop