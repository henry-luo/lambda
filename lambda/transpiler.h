#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <tree_sitter/api.h>
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"

#include "ts-enum.h"

#define SYM_NULL sym_null
#define SYM_TRUE sym_true
#define SYM_FALSE sym_false
#define SYM_INT sym_integer
#define SYM_FLOAT sym_float
#define SYM_STRING sym_string
#define SYM_SYMBOL sym_symbol
#define SYM_IDENT sym_identifier
#define SYM_IF_EXPR sym_if_expr
#define SYM_IF_STAM sym_if_stam
#define SYM_LET_EXPR sym_let_expr
#define SYM_LET_STAM sym_let_stam
#define SYM_FOR_EXPR sym_for_expr
#define SYM_FOR_STAM sym_for_stam
#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_BINARY_EXPR sym_binary_expr
#define SYM_FUNC sym_fn_definition
#define SYM_ARRAY sym_array
#define SYM_MAP sym_map
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_SUBSCRIPT_EXPR sym_subscript_expr
#define SYM_CALL_EXPR sym_call_expr
#define SYM_CONTENT sym_content
#define SYM_DATETIME sym_datetime
#define SYM_TIME sym_time
#define SYM_COMMENT sym_comment

#define FIELD_AS field_as
#define FIELD_COND field_cond
#define FIELD_THEN field_then
#define FIELD_ELSE field_else
#define FIELD_LEFT field_left
#define FIELD_RIGHT field_right
#define FIELD_NAME field_name
#define FIELD_TYPE field_type
#define FIELD_OBJECT field_object
#define FIELD_FIELD field_field
#define FIELD_BODY field_body
#define FIELD_DECLARE field_declare
#define FIELD_FUNCTION field_function
#define FIELD_ARGUMENT field_argument
#define FIELD_OPERATOR field_operator

typedef enum {
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
} Operator ;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"

#include "lambda.h"

typedef struct LambdaType {
    TypeId type_id;
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} LambdaType;

typedef struct {
    LambdaType;  // extends LambdaType
    int const_index;
    double double_val;
} LambdaTypeItem;

typedef struct {
    LambdaType;  // extends LambdaType
    int const_index;
    String *string;
} LambdaTypeString;

typedef struct HeapString {
    LambdaType type;
    // 0: external, const;
    // 1: object itself;
    // 2+: got references;    
    uint16_t ref_cnt;
    void* next; // next heap object
    String; // extends String
} HeapString;

typedef LambdaTypeString LambdaTypeSymbol;

typedef struct {
    LambdaType;  // extends LambdaType
    LambdaType* nested;  // nested item type for the array
    int length;  // no. of items in the array/map
} LambdaTypeArray;

typedef struct ShapeEntry {
    StrView name;
    LambdaType* type;  // type of the field
    int byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
} ShapeEntry;

typedef struct {
    LambdaType;  // extends LambdaType
    int length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
    int byte_size;  // byte size of the struct that the map is transpiled to
    ShapeEntry* shape;  // shape of the map
} LambdaTypeMap;

typedef struct {
    LambdaType;  // extends LambdaType
    LambdaType *param;
    LambdaType *returned;
} LambdaTypeFunc;

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
    AST_NODE_BINARY,
    AST_NODE_ARRAY,
    AST_NODE_LIST,
    AST_NODE_MAP,
    AST_NODE_ASSIGN,
    AST_NODE_LOOP,
    AST_NODE_IF_EXPR,
    AST_NODE_LET_EXPR,
    AST_NODE_FOR_EXPR,
    AST_NODE_LET_STAM,
    AST_NODE_FIELD_EXPR,
    AST_NODE_CALL_EXPR,
    AST_NODE_IDENT,
    AST_NODE_PARAM,
    AST_NODE_FUNC,
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
    AstNode *function, *argument;
} AstCallNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *expr;
} AstPrimaryNode;

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
    AstNode *declare;  // declarations in let expression
    AstNode *then;
} AstLetNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *loop;
    AstNode *then;
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
    AstNode;  // extends AstNode
    AstNamedNode *item;  // first item in the map
} AstMapNode;

// aligned with AstNamedNode
typedef struct {
    AstNode;  // extends AstNode
    StrView name;
    AstNode *body;
    AstNamedNode *param; // first parameter of the function
    NameScope *params;  // params for the function
    NameScope *locals;  // local variables in the function
} AstFuncNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *child;  // first child
    NameScope *global_vars;  // global variables
} AstScript;

typedef struct Heap {
    Pack; // extends Pack
} Heap;
Heap* heap_init(size_t initial_size);
void* heap_alloc(Heap* heap, size_t size);
void* heap_calloc(Heap* heap, size_t size);
void heap_destroy(Heap* heap);

// uses the high byte to tag the pointer, defined for little-endian
typedef union LambdaItem {
    struct {
        union {
            struct {
                uint64_t int_val: 32;
                uint64_t _32: 32;
            };
            struct {
                uint64_t bool_val: 8;
                uint64_t _56: 56;
            };
            struct {
                uint64_t pointer : 56;  // tagged pointer
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

typedef struct {
    const char* source;
    TSParser* parser;
    TSTree* syntax_tree;
    VariableMemPool* ast_pool;
    AstNode *ast_root;
    // todo: have a hashmap to speed up name lookup
    NameScope* current_scope;  // current name scope
    ArrayList* type_list;  // list of types
    ArrayList* const_list;  // list of constants
    StrBuf* code_buf;
    MIR_context_t jit_context;
} Transpiler;

typedef struct {
    Transpiler* transpiler;
    Heap* heap;
    Pack* stack; // eval stack
} Runner;

#define ts_node_source(transpiler, node)  {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

void* alloc_ast_bytes(Transpiler* tp, size_t size);
void* alloc_const(Transpiler* tp, size_t size);
LambdaType* alloc_type(Transpiler* tp, TypeId type, size_t size);
AstNode* build_map_expr(Transpiler* tp, TSNode map_node);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_script(Transpiler* tp, TSNode script_node);
AstNode* print_ast_node(AstNode *node, int indent);
void print_ts_node(TSNode node, uint32_t indent);
void writeNodeSource(Transpiler* tp, TSNode node);
void writeType(Transpiler* tp, LambdaType *type);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);

MIR_context_t jit_init();
void jit_compile(MIR_context_t ctx, const char *code, size_t code_size, char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
void jit_cleanup(MIR_context_t ctx);

typedef uint64_t Item;

void runner_init(Runner* runner);
void runner_cleanup(Runner* runner);
Item run_script(Runner *runner, char* source);
Item run_script_at(Runner *runner, char* script_path);
void print_item(StrBuf *strbuf, Item item);

#pragma clang diagnostic pop