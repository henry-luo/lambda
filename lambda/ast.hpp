#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tree_sitter/api.h>
#include "ts-enum.h"
#include "../lib/mempool.h"

#define SYM_NULL sym_null
#define SYM_TRUE sym_true
#define SYM_FALSE sym_false
#define SYM_INT sym_integer
#define SYM_FLOAT sym_float
#define SYM_DECIMAL sym_decimal
#define SYM_STRING sym_string
#define SYM_SYMBOL sym_symbol
#define SYM_STRING_CONTENT sym_string_content
#define SYM_SYMBOL_CONTENT sym_symbol_content
#define SYM_ESCAPE_SEQUENCE sym_escape_sequence
#define SYM_DATETIME sym_datetime
#define SYM_TIME sym_time
#define SYM_BINARY sym_binary

#define SYM_CONTENT sym_content
#define SYM_LIST sym_list
#define SYM_ARRAY sym_array
#define SYM_MAP_ITEM sym_map_item
#define SYM_MAP sym_map
#define SYM_ELEMENT sym_element
#define SYM_ATTR sym_attr

#define SYM_IDENT sym_identifier
#define SYM_INDEX sym_index
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_INDEX_EXPR sym_index_expr
#define SYM_CALL_EXPR sym_call_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_UNARY_EXPR sym_unary_expr
#define SYM_BINARY_EXPR sym_binary_expr
#define SYM_BINARY_EXPR_NO_PIPE sym_binary_expr_no_pipe

// Path wildcards for glob patterns
#define SYM_PATH_WILDCARD sym_path_wildcard
#define SYM_PATH_WILDCARD_RECURSIVE sym_path_wildcard_recursive

// Path root tokens: / for absolute, . for relative, .. for parent
#define SYM_PATH_ROOT sym_path_root
#define SYM_PATH_SELF sym_path_self
#define SYM_PATH_PARENT sym_path_parent
#define SYM_PATH_EXPR sym_path_expr

// Pipe expression current item references (pipe is now part of binary_expr)
#define SYM_CURRENT_ITEM sym_current_item
#define SYM_CURRENT_INDEX sym_current_index

#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_IF_EXPR sym_if_expr
#define SYM_IF_STAM sym_if_stam
#define SYM_LET_EXPR sym_let_expr
#define SYM_LET_STAM sym_let_stam
#define SYM_PUB_STAM sym_pub_stam
#define SYM_FOR_EXPR sym_for_expr
#define SYM_FOR_STAM sym_for_stam
#define SYM_WHILE_STAM sym_while_stam
#define SYM_BREAK_STAM sym_break_stam
#define SYM_CONTINUE_STAM sym_continue_stam
#define SYM_RETURN_STAM sym_return_stam
#define SYM_VAR_STAM sym_var_stam
#define SYM_ASSIGN_STAM sym_assign_stam

#define SYM_BASE_TYPE sym_base_type
#define SYM_ARRAY_TYPE sym_array_type
#define SYM_LIST_TYPE sym_list_type
#define SYM_MAP_TYPE_ITEM sym_map_type_item
#define SYM_MAP_TYPE sym_map_type
#define SYM_CONTENT_TYPE sym_content_type
#define SYM_ELEMENT_TYPE sym_element_type
#define SYM_FN_TYPE sym_fn_type
#define SYM_PRIMARY_TYPE sym_primary_type
#define SYM_BINARY_TYPE sym_binary_type
#define SYM_TYPE_DEFINE sym_type_stam
#define SYM_TYPE_OCCURRENCE sym_type_occurrence

#define SYM_FUNC_STAM sym_fn_stam
#define SYM_FUNC_EXPR_STAM sym_fn_expr_stam
#define SYM_FUNC_EXPR sym_fn_expr
// #define SYM_SYS_FUNC sym_sys_func
#define SYM_IMPORT_MODULE sym_import_module

// String/Symbol Pattern symbols
#define SYM_STRING_PATTERN sym_string_pattern
#define SYM_SYMBOL_PATTERN sym_symbol_pattern
#define SYM_PATTERN_CHAR_CLASS sym_pattern_char_class
#define SYM_PATTERN_ANY sym_pattern_any
#define SYM_PATTERN_ANY_STAR sym_pattern_any_star
#define SYM_PATTERN_COUNT sym_pattern_count
#define SYM_PRIMARY_PATTERN sym_primary_pattern
#define SYM_PATTERN_OCCURRENCE sym_pattern_occurrence
#define SYM_PATTERN_NEGATION sym_pattern_negation
#define SYM_PATTERN_RANGE sym_pattern_range
#define SYM_BINARY_PATTERN sym_binary_pattern
#define SYM_PATTERN_SEQ sym_pattern_seq
#define SYM_OCCURRENCE_COUNT sym_occurrence_count

#define SYM_COMMENT sym_comment
#define SYM_NAMED_ARGUMENT sym_named_argument

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
#define FIELD_KIND field_kind
#define FIELD_OPTIONAL field_optional
#define FIELD_DEFAULT field_default
#define FIELD_VALUE field_value
#define FIELD_VARIADIC field_variadic
#define FIELD_TARGET field_target
#define FIELD_PATTERN field_pattern
#define FIELD_INDEX field_index
#define FIELD_SEGMENT field_segment
#define FIELD_DECOMPOSE field_decompose
// For expression clause fields
#define FIELD_LET field_let
#define FIELD_WHERE field_where
#define FIELD_GROUP field_group
#define FIELD_ORDER field_order
#define FIELD_LIMIT field_limit
#define FIELD_OFFSET field_offset
#define FIELD_SPEC field_spec
#define FIELD_DIR field_dir
#define FIELD_KEY field_key
#define FIELD_COUNT field_count
#define FIELD_EXPR field_expr

// Symbols for for-expression clauses
#define SYM_FOR_LET_CLAUSE sym_for_let_clause
#define SYM_FOR_WHERE_CLAUSE sym_for_where_clause
#define SYM_ORDER_SPEC sym_order_spec
#define SYM_FOR_ORDER_CLAUSE sym_for_order_clause
#define SYM_FOR_GROUP_CLAUSE sym_for_group_clause
#define SYM_FOR_LIMIT_CLAUSE sym_for_limit_clause
#define SYM_FOR_OFFSET_CLAUSE sym_for_offset_clause

#ifdef __cplusplus
}
#endif

#include "lambda-data.hpp"

typedef struct NamePool NamePool;
typedef struct AstNode AstNode;
typedef struct AstImportNode AstImportNode;

// entry in the name_stack
typedef struct NameEntry {
    String* name;               // Changed from StrView to String* (from name pool)
    AstNode* node;              // AST node that defines the name
    struct NameEntry* next;
    AstImportNode* import;      // the module that the name is imported from, if any
} NameEntry;

// name_scope
typedef struct NameScope {
    NameEntry* first;   // start name entry in the current scope
    NameEntry* last;    // last name entry in the current scope
    bool is_proc;       // whether is inside a procedural scope
    struct NameScope* parent;  // parent scope
} NameScope;

typedef enum AstNodeType {
    AST_NODE_NULL,
    AST_NODE_PRIMARY,
    AST_NODE_UNARY,
    AST_NODE_BINARY,
    AST_NODE_PIPE,          // pipe expression (| and where)
    AST_NODE_CURRENT_ITEM,  // ~ current item reference
    AST_NODE_CURRENT_INDEX, // ~# current key/index reference
    AST_NODE_LIST,
    AST_NODE_CONTENT,
    AST_NODE_ARRAY,
    AST_NODE_MAP,
    AST_NODE_ELEMENT,
    AST_NODE_KEY_EXPR,
    AST_NODE_ASSIGN,
    AST_NODE_DECOMPOSE,     // multi-variable decomposition (let a, b = expr)
    AST_NODE_LOOP,
    AST_NODE_ORDER_SPEC,    // order by specification (expr [asc|desc])
    AST_NODE_GROUP_CLAUSE,  // group by clause
    AST_NODE_IF_EXPR,
    AST_NODE_IF_STAM,
    AST_NODE_FOR_EXPR,
    AST_NODE_FOR_STAM,
    AST_NODE_WHILE_STAM,    // while statement (procedural only)
    AST_NODE_BREAK_STAM,    // break statement (procedural only)
    AST_NODE_CONTINUE_STAM, // continue statement (procedural only)
    AST_NODE_RETURN_STAM,   // return statement (procedural only)
    AST_NODE_VAR_STAM,      // var statement (procedural only)
    AST_NODE_ASSIGN_STAM,   // assignment statement (procedural only)
    AST_NODE_PIPE_FILE_STAM, // pipe to file statement (procedural only): |> and |>>
    AST_NODE_LET_STAM,
    AST_NODE_PUB_STAM,
    AST_NODE_TYPE_STAM,
    AST_NODE_INDEX_EXPR,
    AST_NODE_MEMBER_EXPR,
    AST_NODE_PATH_EXPR,         // path expression (file.etc.hosts, http.api.example.com)
    AST_NODE_PATH_INDEX_EXPR,   // path subscript expression - adds dynamic segment: path[expr]
    AST_NODE_CALL_EXPR,
    AST_NODE_SYS_FUNC,
    AST_NODE_IDENT,
    AST_NODE_PARAM,
    AST_NODE_NAMED_ARG,     // named argument in function call
    AST_NODE_TYPE,  // base type
    AST_NODE_CONTENT_TYPE,
    AST_NODE_LIST_TYPE,
    AST_NODE_ARRAY_TYPE,
    AST_NODE_MAP_TYPE,
    AST_NODE_ELMT_TYPE,
    AST_NODE_FUNC_TYPE,
    AST_NODE_BINARY_TYPE,
    AST_NODE_UNARY_TYPE,
    AST_NODE_FUNC,
    AST_NODE_FUNC_EXPR,
    AST_NODE_PROC, // procedural function
    AST_NODE_IMPORT,
    // String/Symbol Pattern nodes
    AST_NODE_STRING_PATTERN,    // string name = pattern
    AST_NODE_SYMBOL_PATTERN,    // symbol name = pattern
    AST_NODE_PATTERN_RANGE,     // "a" to "z"
    AST_NODE_PATTERN_CHAR_CLASS, // \d, \w, \s, \a, .
    AST_NODE_PATTERN_SEQ,       // sequence of patterns (concatenation)
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
    bool pipe_inject;  // true if this call has an injected first arg from pipe context
} AstCallNode;

// Path segment info for AstPathNode
typedef struct AstPathSegment {
    String* name;           // segment name (NULL for wildcards)
    LPathSegmentType type;   // LPATH_SEG_NORMAL, LPATH_SEG_WILDCARD, etc.
} AstPathSegment;

typedef struct AstPathNode : AstNode {
    PathScheme scheme;           // file, http, https, sys, PATH_RELATIVE, PATH_PARENT
    int segment_count;           // number of path segments
    AstPathSegment* segments;    // array of segment info (allocated in pool)
} AstPathNode;

// Path index expression: path[expr] - adds a dynamic segment to the path
// Unlike regular index_expr, this extends the path with a runtime-computed segment
typedef struct AstPathIndexNode : AstNode {
    AstNode* base_path;      // the base path expression
    AstNode* segment_expr;   // expression for the dynamic segment
} AstPathIndexNode;

typedef struct SysFuncInfo {
    SysFunc fn;
    const char* name;
    int arg_count;  // -1 for variable args
    Type* return_type;
    bool is_proc;   // is procedural
    bool is_overloaded;
    bool is_method_eligible;    // can be called as obj.method() style
    TypeId first_param_type;    // expected type of first param (LMD_TYPE_ANY for any)
} SysFuncInfo;

typedef struct AstSysFuncNode : AstNode {
    SysFuncInfo* fn_info;
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

// Pipe expression (| and where) - same structure as binary, different semantics
typedef AstBinaryNode AstPipeNode;

// for AST_NODE_ASSIGN, AST_NODE_KEY_EXPR, AST_NODE_PARAM
typedef struct AstNamedNode : AstNode {
    String* name;               // Changed from StrView to String* (from name pool)
    AstNode *as;
} AstNamedNode;

// for AST_NODE_LOOP - extended with index variable and named flag
typedef struct AstLoopNode : AstNode {
    String* name;               // primary loop variable (v in 'for v in expr')
    String* index_name;         // optional index variable (i in 'for i, v in expr'), NULL if not present
    AstNode *as;                // collection expression
    bool is_named;              // true if 'at' keyword used (attribute/named iteration)
} AstLoopNode;

// for AST_NODE_ASSIGN with decomposition (let a, b = expr / let a, b at expr)
typedef struct AstDecomposeNode : AstNode {
    String** names;             // array of variable names
    int name_count;             // number of variables
    AstNode *as;                // source expression
    bool is_named;              // true if 'at' keyword used (named decomposition)
} AstDecomposeNode;

typedef struct AstIdentNode : AstNode {
    String* name;               // Changed from StrView to String* (from name pool)
    NameEntry *entry;
} AstIdentNode;

struct AstImportNode : AstNode {
    String* alias;              // Changed from StrView to String* (from name pool)
    StrView module;             // Keep module as StrView (file path)
    Script* script;             // imported script
    bool is_relative;
};

typedef struct AstLetNode : AstNode {
    AstNode *declare;  // declarations in let expression
} AstLetNode;

// Order specification within for-expression: expr [asc|desc]
typedef struct AstOrderSpec : AstNode {
    AstNode *expr;      // expression to order by
    bool descending;    // true if 'desc' or 'descending'
} AstOrderSpec;

// Group clause: group by expr, expr, ... as name
typedef struct AstGroupClause : AstNode {
    AstNode *keys;      // linked list of key expressions
    String* name;       // alias name (from 'as name')
} AstGroupClause;

typedef struct AstForNode : AstNode {
    AstNode *loop;       // loop bindings (linked list of AstLoopNode)
    AstNode *let_clause; // let bindings (linked list of AstNamedNode)
    AstNode *where;      // where condition (single expression, or NULL)
    AstGroupClause *group; // group by clause (or NULL)
    AstNode *order;      // order by specs (linked list of AstOrderSpec, or NULL)
    AstNode *limit;      // limit count expression (or NULL)
    AstNode *offset;     // offset count expression (or NULL)
    AstNode *then;       // body expression
    NameScope *vars;     // scope for the variables in the loop
} AstForNode;

typedef struct AstIfNode : AstNode {
    AstNode *cond;
    AstNode *then;
    AstNode *otherwise;
} AstIfNode;

// while statement (procedural only)
typedef struct AstWhileNode : AstNode {
    AstNode *cond;
    AstNode *body;
    NameScope *vars;  // scope for the variables in the while
} AstWhileNode;

// return statement (procedural only)
typedef struct AstReturnNode : AstNode {
    AstNode *value;  // optional return value
} AstReturnNode;

// assignment statement (procedural only)
typedef struct AstAssignStamNode : AstNode {
    String* target;       // variable name to assign to
    AstNode *target_node; // AST node of the target variable (for type info)
    AstNode *value;       // value expression
} AstAssignStamNode;

typedef struct AstArrayNode : AstNode {
    AstNode *item;  // first item in the array
} AstArrayNode;

typedef struct AstListNode : AstArrayNode {
    AstNode *declare;  // declarations in the list
    NameScope *vars;  // scope for the variables in the list
    TypeList* list_type;
} AstListNode;

typedef struct AstMapNode : AstNode {
    AstNode *item;  // first item in the map
} AstMapNode;

typedef struct AstElementNode : AstMapNode {
    AstNode *content;  // first content node
} AstElementNode;

// ==================== String/Symbol Pattern AST Nodes ====================

// Pattern definition node (string name = pattern OR symbol name = pattern)
// Extends AstNamedNode so it has 'name' and 'as' (the pattern expression)
typedef struct AstPatternDefNode : AstNamedNode {
    bool is_symbol;     // true for symbol pattern, false for string pattern
} AstPatternDefNode;

// Pattern range node ("a" to "z")
typedef struct AstPatternRangeNode : AstNode {
    AstNode* start;     // start of range (string literal)
    AstNode* end;       // end of range (string literal)
} AstPatternRangeNode;

// Pattern character class node (\d, \w, \s, \a, .)
typedef struct AstPatternCharClassNode : AstNode {
    PatternCharClass char_class;
} AstPatternCharClassNode;

// Pattern sequence node (concatenation of patterns)
typedef struct AstPatternSeqNode : AstNode {
    AstNode* first;     // first pattern in sequence (linked list via 'next')
} AstPatternSeqNode;

// Forward declare for capture list
struct CaptureInfo;

// aligned with AstNamedNode on name
typedef struct AstFuncNode : AstNode {
    String* name;               // Changed from StrView to String* (from name pool)
    AstNamedNode *param;        // first parameter of the function
    AstNode *body;
    NameScope *vars;            // vars including params and local variables
    struct CaptureInfo* captures; // list of captured variables (NULL if no captures)
} AstFuncNode;

// Capture info for closures
typedef struct CaptureInfo {
    String* name;              // captured variable name
    NameEntry* entry;          // reference to the captured variable's scope entry
    bool is_mutable;           // true if the captured variable is modified
    struct CaptureInfo* next;  // next capture in list
} CaptureInfo;

// root of the AST
typedef struct AstScript : AstNode {
    AstNode *child;  // first child
    NameScope *global_vars;  // global variables
} AstScript;

typedef Item (*main_func_t)(Context*);
typedef struct MIR_context *MIR_context_t;

// Script extends Input to inherit unified memory management
struct Script : Input {
    const char* reference;      // path (relative to the main script) and name of the script
    int index;                  // index of the script in the runtime scripts list
    const char* source;
    TSTree* syntax_tree;

    // AST-specific fields (beyond Input)
    AstNode *ast_root;
    NameScope* current_scope;   // current name scope
    ArrayList* const_list;      // list of constants (Script-specific)

    // JIT compilation (Script-specific)
    MIR_context_t jit_context;
    main_func_t main_func;      // transpiled main function
    mpd_context_t* decimal_ctx;  // libmpdec context for decimal operations

    // Debug info for stack traces (function address → source mapping)
    ArrayList* debug_info;      // list of FuncDebugInfo*

    // Function name mapping: MIR internal name → Lambda human-readable name
    // Used by build_debug_info_table() to get user-friendly names
    struct hashmap* func_name_map;  // maps char* (MIR name) → char* (Lambda name)
};

typedef struct Runtime Runtime;
struct LambdaError;  // forward declaration

typedef struct Transpiler : Script {
    TSParser* parser;
    StrBuf* code_buf;
    Runtime* runtime;

    // Error tracking for accumulated type errors
    int error_count;           // accumulated error count
    int max_errors;            // threshold (default: 10)
    ArrayList* errors;         // list of LambdaError* (structured errors)

    // Closure transpilation context
    AstFuncNode* current_closure;  // non-null when transpiling inside a closure body

    // Assignment name context (for naming anonymous closures)
    String* current_assign_name;  // name of variable being assigned (e.g., "level1" for let level1 = fn...)

    // Tail Call Optimization context
    AstFuncNode* tco_func;     // non-null when transpiling body of a TCO-enabled function
    bool in_tail_position;     // true when current expression is in tail position

    // Unboxed function transpilation context
    bool in_unboxed_body;      // true when transpiling body of unboxed (_u) version

    // Pipe injection context (for data | func(args) -> func(data, args))
    int pipe_inject_args;      // extra args to add when looking up sys_func (0 normally, 1 in pipe context)
} Transpiler;

// Helper to check if arg_type is compatible with param_type
bool types_compatible(Type* arg_type, Type* param_type);

void print_item(StrBuf *strbuf, Item item, int depth=0, char* indent="  ");
void print_root_item(StrBuf *strbuf, Item item, char* indent="  ");
// for C to access
extern "C" void format_item(StrBuf *strbuf, Item item, int depth, char* indent);

// for debugging onnly
void log_item(Item item, const char* msg="");
