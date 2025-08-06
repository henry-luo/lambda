#pragma once

#include "lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"

typedef struct AstNode AstNode;
typedef struct AstImportNode AstImportNode;

// entry in the name_stack
typedef struct NameEntry {
    StrView name;
    AstNode* node;  // AST node that defines the name
    struct NameEntry* next;
    AstImportNode* import;  // the module that the name is imported from, if any
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
    AST_NODE_PUB_STAM,
    AST_NODE_INDEX_EXPR,
    AST_NODE_MEMBER_EXPR,
    AST_NODE_CALL_EXPR,
    AST_NODE_SYS_FUNC,
    AST_NODE_IDENT,
    AST_NODE_PARAM,
    AST_NODE_TYPE,
    AST_NODE_CONTENT_TYPE,
    AST_NODE_LIST_TYPE,
    AST_NODE_ARRAY_TYPE,
    AST_NODE_MAP_TYPE,
    AST_NODE_ELMT_TYPE,
    AST_NODE_FUNC_TYPE,
    AST_NODE_BINARY_TYPE,
    AST_NODE_FUNC,
    AST_NODE_FUNC_EXPR,
    AST_NODE_IMPORT,
    AST_SCRIPT,
} AstNodeType;

struct AstNode {
    AstNodeType node_type;
    Type *type;
    struct AstNode* next;
    TSNode node;
};

typedef struct AstFieldNode : AstNode {
    AstNode *object, *field;
} AstFieldNode;

typedef struct AstCallNode : AstNode {
    AstNode *function;
    AstNode *argument;
} AstCallNode;

typedef struct AstSysFuncNode : AstNode {
    SysFunc fn;
} AstSysFuncNode;

typedef struct AstPrimaryNode : AstNode {
    AstNode *expr;
} AstPrimaryNode;

typedef AstNode AstTypeNode;

typedef struct AstUnaryNode : AstNode {
    AstNode *operand;
    StrView op_str;
    Operator op;
} AstUnaryNode;

typedef struct AstBinaryNode : AstNode {
    AstNode *left, *right;
    StrView op_str;
    Operator op;
} AstBinaryNode;

// for AST_NODE_ASSIGN, AST_NODE_KEY_EXPR, AST_NODE_LOOP, AST_NODE_PARAM
typedef struct AstNamedNode : AstNode {
    StrView name;
    AstNode *as;
} AstNamedNode;

typedef struct AstIdentNode : AstNode {
    StrView name;
    NameEntry *entry;
} AstIdentNode;

struct AstImportNode : AstNode {
    StrView alias;
    StrView module;
    Script* script; // imported script
    bool is_relative;
};

typedef struct AstLetNode : AstNode {
    AstNode *declare;  // declarations in let expression
} AstLetNode;

typedef struct AstForNode : AstNode {
    AstNode *loop;
    AstNode *then;
    NameScope *vars;  // scope for the variables in the loop
} AstForNode;

typedef struct AstIfExprNode : AstNode {
    AstNode *cond;
    AstNode *then;
    AstNode *otherwise;
} AstIfExprNode;

typedef struct AstArrayNode : AstNode {
    AstNode *item;  // first item in the array
} AstArrayNode;

typedef struct AstListNode : AstArrayNode {
    AstNode *declare;  // declarations in the list
    NameScope *vars;  // scope for the variables in the list
} AstListNode;

typedef struct AstMapNode : AstNode {
    AstNode *item;  // first item in the map
} AstMapNode;

typedef struct AstElementNode : AstMapNode {
    AstNode *content;  // first content node
} AstElementNode;

// aligned with AstNamedNode on name
typedef struct AstFuncNode : AstNode {
    StrView name;
    AstNamedNode *param; // first parameter of the function
    AstNode *body;
    NameScope *vars;  // vars including params and local variables
} AstFuncNode;

// root of the AST
typedef struct AstScript : AstNode {
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

#ifndef WASM_BUILD
#include <mir.h>
#include <mir-gen.h>
#include <c2mir.h>
#else
#include "../wasm-deps/include/mir.h"
#include "../wasm-deps/include/mir-gen.h"
#include "../wasm-deps/include/c2mir.h"
#endif

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

typedef struct Transpiler : Script {
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

void* alloc_const(Transpiler* tp, size_t size);
AstNode* build_map(Transpiler* tp, TSNode map_node);
AstNode* build_elmt(Transpiler* tp, TSNode element_node);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global);
AstNode* build_script(Transpiler* tp, TSNode script_node);
void print_ast_node(AstNode *node, int indent);
void print_ts_node(const char *source, TSNode node, uint32_t indent);
void find_errors(TSNode node);
void writeNodeSource(Transpiler* tp, TSNode node);
void writeType(Transpiler* tp, Type *type);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import);

MIR_context_t jit_init();
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void* find_data(MIR_context_t ctx, const char *data_name);
void jit_cleanup(MIR_context_t ctx);

// MIR transpiler functions
Item run_script_mir(Runtime *runtime, const char* source, char* script_path);

Script* load_script(Runtime *runtime, const char* script_path, const char* source);
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);
Item run_script(Runtime *runtime, const char* source, char* script_path);
Item run_script_at(Runtime *runtime, char* script_path);
void print_item(StrBuf *strbuf, Item item);

void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);

#pragma clang diagnostic pop

#ifdef __cplusplus
}
#endif