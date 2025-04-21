#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tree_sitter/api.h>
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"

enum ts_symbol_identifiers {
    sym_identifier = 1,
    anon_sym_LBRACE = 2,
    anon_sym_COLON = 3,
    anon_sym_COMMA = 4,
    anon_sym_RBRACE = 5,
    anon_sym_LBRACK = 6,
    anon_sym_RBRACK = 7,
    anon_sym_DQUOTE = 8,
    sym_string_content = 9,
    anon_sym_SQUOTE = 10,
    sym_symbol_content = 11,
    sym_escape_sequence = 12,
    sym_number = 13,
    sym_integer = 14,
    sym_true = 15,
    sym_false = 16,
    sym_null = 17,
    sym_comment = 18,
    anon_sym_LPAREN = 19,
    anon_sym_RPAREN = 20,
    sym_import = 21,
    anon_sym_DOT_DOT_DOT = 22,
    anon_sym_DOT = 23,
    anon_sym_and = 24,
    anon_sym_or = 25,
    anon_sym_PLUS = 26,
    anon_sym_DASH = 27,
    anon_sym_STAR = 28,
    anon_sym_SLASH = 29,
    anon_sym_PERCENT = 30,
    anon_sym_STAR_STAR = 31,
    anon_sym_LT = 32,
    anon_sym_LT_EQ = 33,
    anon_sym_EQ_EQ = 34,
    anon_sym_BANG_EQ = 35,
    anon_sym_GT_EQ = 36,
    anon_sym_GT = 37,
    anon_sym_to = 38,
    anon_sym_in = 39,
    anon_sym_not = 40,
    anon_sym_fn = 41,
    anon_sym_EQ = 42,
    anon_sym_let = 43,
    anon_sym_if = 44,
    anon_sym_else = 45,
    anon_sym_for = 46,
    anon_sym_SEMI = 47,
    sym_document = 48,
    sym__statement = 49,
    sym_lit_map = 50,
    sym_pair = 51,
    sym_map = 52,
    sym_lit_array = 53,
    sym_array = 54,
    sym_string = 55,
    aux_sym__string_content = 56,
    sym_symbol = 57,
    aux_sym__symbol_content = 58,
    sym__expression = 59,
    sym_primary_expr = 60,
    sym_spread_element = 61,
    sym_call_expr = 62,
    sym_subscript_expr = 63,
    sym_member_expr = 64,
    sym_binary_expr = 65,
    sym_unary_expr = 66,
    sym_parameter = 67,
    sym_fn_definition = 68,
    sym_assign_expr = 69,
    sym_let_expr = 70,
    sym_if_expr = 71,
    sym_loop_expr = 72,
    sym_for_expr = 73,
    sym_let_stam = 74,
    aux_sym_document_repeat1 = 75,
    aux_sym_lit_map_repeat1 = 76,
    aux_sym_map_repeat1 = 77,
    aux_sym_lit_array_repeat1 = 78,
    aux_sym_array_repeat1 = 79,
    aux_sym__arguments_repeat1 = 80,
    aux_sym_fn_definition_repeat1 = 81,
    aux_sym_let_expr_repeat1 = 82,
    aux_sym_for_expr_repeat1 = 83,
};

enum ts_field_identifiers {
    field_argument = 1,
    field_body = 2,
    field_cond = 3,
    field_declare = 4,
    field_else = 5,
    field_field = 6,
    field_function = 7,
    field_left = 8,
    field_name = 9,
    field_object = 10,
    field_operator = 11,
    field_right = 12,
    field_then = 13,
};

#define SYM_NULL sym_null
#define SYM_TRUE sym_true
#define SYM_FALSE sym_false
#define SYM_NUMBER sym_number
#define SYM_STRING sym_string
#define SYM_IDENT sym_identifier
#define SYM_IF_EXPR sym_if_expr
#define SYM_LET_EXPR sym_let_expr
#define SYM_FOR_EXPR sym_for_expr
#define SYM_LET_STAM sym_let_stam
#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_BINARY_EXPR sym_binary_expr
#define SYM_FUNC sym_fn_definition
#define SYM_ARRAY sym_array
#define SYM_MAP sym_map
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_SUBSCRIPT_EXPR sym_subscript_expr
#define SYM_CALL_EXPR sym_call_expr

#define FIELD_COND field_cond
#define FIELD_THEN field_then
#define FIELD_ELSE field_else
#define FIELD_LEFT field_left
#define FIELD_RIGHT field_right
#define FIELD_NAME field_name
#define FIELD_OBJECT field_object
#define FIELD_FIELD field_field
#define FIELD_BODY field_body
#define FIELD_DECLARE field_declare
#define FIELD_FUNCTION field_function
#define FIELD_ARGUMENT field_argument

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"

typedef enum TypeId {
    LMD_TYPE_NULL,
    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,
    LMD_TYPE_FLOAT,
    LMD_TYPE_STRING,
    LMD_TYPE_ARRAY,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_FUNC,
} TypeId;

typedef struct LambdaType {
    TypeId type_id;
} LambdaType;

typedef struct LambdaTypeArray {
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

typedef struct LambdaTypeMap {
    LambdaType;  // extends LambdaType
    int length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
    int byte_size;  // byte size of the struct that the map is transpiled to
    ShapeEntry* shape;  // shape of the map
} LambdaTypeMap;

typedef struct Pack {
    size_t size;           // Current used size of the pack
    size_t capacity;       // Total capacity of the pack
    size_t committed_size; // Currently committed memory size - non-zero indicates virtual memory mode
    void* data;            // Pointer to the allocated memory
} Pack;
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
} AstBinaryNode;

typedef struct {
    AstNode;  // extends AstNode
    StrView name;
    AstNode *then;
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

typedef struct {
    AstNode;  // extends AstNode
    TSNode name;
    AstNamedNode *param; // first parameter of the function
    AstNode *body;
    NameScope *params;  // params for the function
    NameScope *locals;  // local variables in the function
} AstFuncNode;

typedef struct {
    AstNode;  // extends AstNode
    AstNode *child;  // first child
    NameScope *global_vars;  // global variables
} AstScript;

typedef enum {
    TP_DECLARE,  // var declaration phase
    TP_COMPOSE,  // expr composition phase
} TranspilePhase;

typedef struct Heap {
    Pack; // extends Pack
} Heap;
Heap* heap_init(size_t initial_size);
void* heap_alloc(Heap* heap, size_t size);
void* heap_calloc(Heap* heap, size_t size);
void heap_destroy(Heap* heap);

// uses the high byte to tag the pointer
typedef union LambdaItem {
    struct {
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint64_t type_id : 8;
        uint64_t value   : 56;
        #else
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
                uint64_t value   : 48;
                uint64_t type_id : 16;                
            };
        };
        #endif
    };
    uint64_t item;
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
    TranspilePhase phase;
    NameScope* current_scope;  // current name scope
    ArrayList* type_list;  // list of types
    StrBuf* code_buf;
    MIR_context_t jit_context;
} Transpiler;

typedef struct {
    Transpiler* transpiler;
    Heap* heap;
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

MIR_context_t jit_init();
void jit_compile(MIR_context_t ctx, const char *code, size_t code_size, char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
void jit_cleanup(MIR_context_t ctx);

#pragma clang diagnostic pop