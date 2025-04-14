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

typedef enum LambdaTypeId {
    LMD_TYPE_NULL,
    LMD_TYPE_INT,
    LMD_TYPE_FLOAT,
    LMD_TYPE_STRING,
    LMD_TYPE_BOOL,
    LMD_TYPE_ARRAY,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_FUNC,
} LambdaTypeId;

typedef struct LambdaType {
    LambdaTypeId type;
    struct LambdaType* nested;  // nested type
    int length;  // length of array
} LambdaType;

typedef struct ShapeField {
    StrView name;  // field name
    LambdaType type;  // field type
    struct ShapeField* next;  // next field
} ShapeField;

typedef struct Shape {
    ShapeField* first_field;  // field list
    int field_count;  // number of fields
    int byte_size;  // size of the shape
} Shape;

// pack based on virtual memory
typedef struct VirtualPack {
    size_t page_size;       // Size of each page (e.g., 4K)
    size_t total_size;      // Total reserved memory size (e.g., 64K)
    size_t committed_size;  // Currently committed memory size
    size_t used_size;       // Used memory size within committed pages
    void* base_address;     // Base address of the reserved memory
} VirtualPack;

typedef struct Pack {
    size_t size;           // Current used size of the pack
    size_t capacity;       // Total capacity of the pack
    size_t committed_size; // Currently committed memory size - non-zero indicates virtual memory mode
    void* data;            // Pointer to the allocated memory
} Pack;

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
    AST_NODE_ASSIGN,
    AST_NODE_LOOP,
    AST_NODE_IF_EXPR,
    AST_NODE_LET_EXPR,
    AST_NODE_FOR_EXPR,
    AST_NODE_LET_STAM,
    AST_NODE_FUNC,
    AST_SCRIPT,
    AST_NODE_FIELD_EXPR,
} AstNodeType;

struct AstNode {
    AstNodeType node_type;
    TSNode node;
    LambdaType type;
    struct AstNode* next;
};

typedef struct {
    AstNode;  // extends AstNode
    AstNode *object, *field;
} AstFieldNode;

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
} AstAssignNode;

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
    TSNode name;
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

typedef struct {
    StrBuf* code_buf;
    const char* source;
    VariableMemPool* ast_node_pool;
    AstNode *ast_root;
    // todo: have a hashmap to speed up name lookup
    TranspilePhase phase;
    NameScope* current_scope;  // current name scope

    TSSymbol SYM_NULL;
    TSSymbol SYM_TRUE;
    TSSymbol SYM_FALSE;
    TSSymbol SYM_NUMBER;
    TSSymbol SYM_STRING;
    TSSymbol SYM_ARRAY;
    TSSymbol SYM_PRIMARY_EXPR;
    TSSymbol SYM_BINARY_EXPR;
    TSSymbol SYM_IF_EXPR;
    TSSymbol SYM_LET_EXPR;
    TSSymbol SYM_FOR_EXPR;
    TSSymbol SYM_LET_STAM;
    TSSymbol SYM_ASSIGN_EXPR;
    TSSymbol SYM_LOOP_EXPR;
    TSSymbol SYM_FUNC;
    TSSymbol SYM_IDENTIFIER;
    TSSymbol SYM_MEMBER_EXPR;
    TSSymbol SYM_SUBSCRIPT_EXPR;
    TSFieldId ID_COND;
    TSFieldId ID_THEN;
    TSFieldId ID_ELSE;
    TSFieldId ID_LEFT;
    TSFieldId ID_RIGHT;
    TSFieldId ID_NAME;
    TSFieldId ID_BODY;
    TSFieldId ID_DECLARE;
    TSFieldId ID_OBJECT;
    TSFieldId ID_FIELD;
} Transpiler;

void* alloc_ast_bytes(Transpiler* tp, size_t size);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_script(Transpiler* tp, TSNode script_node);
AstNode* print_ast_node(AstNode *node, int indent);
void print_ts_node(TSNode node, uint32_t indent);

#include <mir.h>
#include <mir-gen.h>
#include <c2mir.h>
MIR_context_t jit_init();
void* jit_compile(MIR_context_t ctx, const char *code, size_t code_size, char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
void jit_cleanup(MIR_context_t ctx);