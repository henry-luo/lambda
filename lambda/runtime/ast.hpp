#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tree_sitter/api.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "ts-enum.h"
#include "../../lib/mempool.h"

#define SYM_NULL sym_null
#define SYM_NAMED_VALUE sym_named_value
#define SYM_INT sym_integer
#define SYM_FLOAT sym_float
#define SYM_IMAGINARY sym_imaginary
#define SYM_DECIMAL sym_decimal
#define SYM_SIZED_INT sym_sized_integer
#define SYM_SIZED_FLOAT sym_sized_float
#define SYM_STRING sym_string
#define SYM_SYMBOL sym_symbol
// Note: string_content, symbol_content, escape_sequence no longer exist
// as strings and symbols are now single tokens that include escapes
#define SYM_DATETIME sym_datetime
#define SYM_BINARY sym_binary

#define SYM_CONTENT sym_content
#define SYM_ARRAY sym_array
#define SYM_MAP_ITEM sym_map_item
#define SYM_MAP sym_map
#define SYM_ELEMENT sym_element
#define SYM_ATTR sym_attr

#define SYM_IDENT sym_identifier
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_INDEX_EXPR sym_index_expr
#define SYM_CALL_EXPR sym_call_expr
#define SYM_QUERY_EXPR sym_query_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_UNARY_EXPR sym_unary_expr
#define SYM_BINARY_EXPR sym_binary_expr
#define SYM_BINARY_EXPR_NO_PIPE sym_binary_expr_no_pipe
#define SYM_EXPR sym__expr
#define SYM_EXPR_NO_PIPE sym_expr_no_pipe
#define SYM_TYPE_EXPR sym__type_expr

// Path wildcards for glob patterns
#define SYM_PATH_WILDCARD sym_path_wildcard

// Path/navigation tokens.
#define SYM_PATH_PARENT sym_path_parent
#define SYM_PATH_ROOT sym_path_root
#define SYM_PATH_EXPR sym_path_expr
#define SYM_NAV_EXPR sym_nav_expr
#define SYM_CURRENT_PARENT_EXPR sym_current_parent_expr

// Pipe expression current item references (pipe is now part of binary_expr)
#define SYM_CURRENT_EXPR sym_current_expr
#define SYM_CURRENT_ERROR_EXPR sym_current_error_expr
#define SYM_LAST_INDEX sym_last_index

#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_IF_EXPR sym_if_expr
#define SYM_IF_STAM sym_if_stam
#define SYM_MATCH_EXPR sym_match_expr
#define SYM_MATCH_ARM sym_match_arm
#define SYM_MATCH_DEFAULT sym_match_default
#define SYM_LET_EXPR sym_let_expr
#define SYM_LET_STAM sym_let_stam
#define SYM_FOR_EXPR sym_for_expr
#define SYM_FOR_STAM sym_for_stam
#define SYM_WHILE_STAM sym_while_stam
#define SYM_BREAK_STAM sym_break_stam
#define SYM_CONTINUE_STAM sym_continue_stam
#define SYM_RETURN_STAM sym_return_stam
#define SYM_RAISE_STAM sym_raise_stam
#define SYM_RAISE_EXPR sym_raise_expr
#define SYM_VAR_STAM sym_var_stam
#define SYM_ASSIGN_STAM sym_assign_stam
#define SYM_APPLY_STAM sym_apply_stam

#define SYM_BASE_TYPE sym_base_type
#define SYM_ARRAY_TYPE sym_array_type
#define SYM_LIST_TYPE sym_list_type
#define SYM_MAP_TYPE_ITEM sym_map_type_item
#define SYM_MAP_TYPE sym_map_type
#define SYM_CONTENT_TYPE sym_content_type
#define SYM_ELEMENT_TYPE sym_element_type
#define SYM_FN_TYPE sym_fn_type
#define SYM_RANGE_TYPE sym_range_type
#define SYM_RETURN_TYPE sym_return_type
#define SYM_RETURN_TYPE_PATTERN sym_return_type_pattern
#define SYM_RETURN_OCCURRENCE_TYPE sym_return_occurrence_type
#define SYM_PRIMARY_TYPE sym_primary_type
#define SYM_BINARY_TYPE sym_binary_type
#define SYM_CONSTRAINED_TYPE sym_constrained_type
#define SYM_GROUPED_TYPE sym_grouped_type
#define SYM_TYPE_DEFINE sym_type_stam
#define SYM_TYPE_OCCURRENCE sym_type_occurrence

#define SYM_FUNC_STAM sym_fn_stam
#define SYM_FUNC_EXPR_STAM sym_fn_expr_stam
#define SYM_FUNC_EXPR sym_fn_expr
// #define SYM_SYS_FUNC sym_sys_func
#define SYM_IMPORT_MODULE sym_import_module

// Object type definition symbols
#define SYM_OBJECT_TYPE sym_object_type
#define SYM_THAT_CONSTRAINT sym_that_constraint

// String/Symbol Pattern symbols
#define SYM_PATTERN_CHAR_CLASS sym_pattern_char_class
#define SYM_PATTERN_ISLAND sym_pattern_island
#define SYM_PATTERN_OCCURRENCE_TYPE sym_pattern_occurrence_type
#define SYM_PATTERN_NEGATION_TYPE sym_pattern_negation_type
#define SYM_PATTERN_UNARY_TYPE sym_pattern_unary_type
// SYM_PATTERN_ANY removed — merged into SYM_PATTERN_CHAR_CLASS
#define SYM_OCCURRENCE_COUNT sym_occurrence_count
// Unified type/pattern symbols
#define SYM_TYPE_SEQ sym_type_seq
#define SYM_PATTERN_GROUP sym_pattern_group
#define SYM_TYPE_NEGATION sym_type_negation

#define SYM_COMMENT sym_comment
#define SYM_NAMED_ARGUMENT sym_named_argument
#define SYM_START_EXPR sym_start_expr

// View/Edit template symbols
#define SYM_VIEW_STAM sym_view_stam
#define SYM_VIEW_PATTERN sym_view_pattern
#define SYM_STATE_DECL sym_state_decl
#define SYM_STATE_ENTRY sym_state_entry
#define SYM_EVENT_HANDLER sym_event_handler
#define SYM_HANDLER_EXPR sym_handler_expr
#define SYM_PROPAGATE_EXPR sym_propagate_expr

#define FIELD_COND field_cond
#define FIELD_THEN field_then
#define FIELD_ELSE field_else
#define FIELD_SCRUTINEE field_scrutinee
#define FIELD_LEFT field_left
#define FIELD_RIGHT field_right
#define FIELD_LAST field_last
#define FIELD_NAME field_name
#define FIELD_AS field_as
#define FIELD_TYPE field_type
#define FIELD_OBJECT field_object
#define FIELD_OP field_op
#define FIELD_FIELD field_field
#define FIELD_BODY field_body
#define FIELD_TAG field_tag
#define FIELD_DECLARE field_declare
#define FIELD_FUNCTION field_function
#define FIELD_ARGUMENT field_argument
#define FIELD_OPERATOR field_operator
#define FIELD_OPERATION field_operation
#define FIELD_OPERAND field_operand
#define FIELD_ALIAS field_alias
#define FIELD_MODULE field_module
#define FIELD_PUB field_pub
#define FIELD_KIND field_kind
#define FIELD_ON field_on
#define FIELD_OPTIONAL field_optional
#define FIELD_DEFAULT field_default
#define FIELD_VALUE field_value
#define FIELD_VARIADIC field_variadic
#define FIELD_VAR field_var
#define FIELD_TARGET field_target
#define FIELD_PREFIX field_prefix
#define FIELD_URI field_uri
#define FIELD_PATTERN field_pattern
#define FIELD_INDEX field_index
#define FIELD_INDEX_TYPE field_index_type
#define FIELD_SEGMENT field_segment
#define FIELD_DECOMPOSE field_decompose
#define FIELD_BASE field_base
#define FIELD_CONSTRAINT field_constraint
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
#define FIELD_ERROR field_error
#define FIELD_PROPAGATE field_propagate
#define FIELD_QUERY field_query
#define FIELD_START field_start
#define FIELD_END field_end
// View/Edit template fields
#define FIELD_STATE field_state
#define FIELD_HANDLER field_handler
#define FIELD_EVENT field_event

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

#include "../lambda-data.hpp"
#include "../core/print.h"
#include "sys_func_registry.h"

typedef struct JubeModuleImport {
    String* module;
    String* alias;
    struct JubeModuleImport* next;
} JubeModuleImport;

// Query expression: expr?T (recursive) or expr.?T (direct)
typedef struct AstQueryNode : AstNode {
    AstNode *object;     // the data to search
    AstNode *query;      // the type pattern to match (primary_type)
    bool direct;         // true for .? (self-inclusive query)
} AstQueryNode;

// Path segment info for AstPathNode
typedef struct AstPathSegment {
    String* name;           // segment name (NULL for wildcards)
    LPathSegmentType type;   // LPATH_SEG_NORMAL, LPATH_SEG_WILDCARD, etc.
    int64_t int_value;      // value for LPATH_SEG_INT
} AstPathSegment;

typedef struct AstPathNode : AstNode {
    PathScheme scheme;           // logical, file, http, https, sys, or relative
    bool file_local;              // `file./` rather than `file.hostname`
    String* authority;            // named file authority, if present
    int segment_count;           // number of path segments
    AstPathSegment* segments;    // array of segment info (allocated in pool)
} AstPathNode;

// Path index expression: path[expr] - adds a dynamic segment to the path
// Unlike regular index_expr, this extends the path with a runtime-computed segment
typedef struct AstPathIndexNode : AstNode {
    AstNode* base_path;      // the base path expression
    AstNode* segment_expr;   // expression for the dynamic segment
} AstPathIndexNode;

typedef struct AstNavigationNode : AstNode {
    AstNode* object;
    bool root;                // true for ./, false for .~~
} AstNavigationNode;

// CRetType, CArgConvention, and SysFuncInfo are now in sys_func_registry.h

typedef struct AstSysFuncNode : AstNode {
    SysFuncInfo* fn_info;
} AstSysFuncNode;

// Constrained type: type where (constraint)
// e.g., int where (5 < ~ < 10), string where (len(~) > 0)
typedef struct AstConstrainedTypeNode : AstNode {
    AstNode *base;          // base type (e.g., int, string)
    AstNode *constraint;    // constraint expression (uses ~ to refer to value)
} AstConstrainedTypeNode;

// Loop key filter: controls which entries to iterate
enum LoopKeyFilter {
    LOOP_KEY_ALL    = 0,  // all entries (default): for k, v in container
    LOOP_KEY_INT    = 1,  // indexed only: for k:int, v in container
    LOOP_KEY_SYMBOL = 2,  // keyed only: for k:symbol, v in container
};

// for AST_NODE_LOOP - extended with index variable and key filter
typedef struct AstJoinKey : AstNode {
    AstNode* prior_expr;        // key expression evaluated against the tuple stream so far
    AstNode* new_expr;          // key expression evaluated against this loop source
} AstJoinKey;

typedef struct AstLoopNode : AstNode {
    String* name;               // primary loop variable (v in 'for v in expr')
    String* index_name;         // optional index variable (k in 'for k, v in expr'), NULL if not present
    AstNode *as;                // collection expression
    AstNode *on;                // optional join condition (`on a.k == b.k`)
    AstJoinKey* join_keys;      // linked list of equi-join key pairs
    LoopKeyFilter key_filter;   // key type filter (ALL, INT, SYMBOL)
    bool key_only;              // for k at expr: bind primary variable to keys, not values
    bool optional;              // true for null-padded left join (`name? in ... on ...`)
    int join_key_count;
} AstLoopNode;

// Order specification within for-expression: expr [asc|desc]
typedef struct AstOrderSpec : AstNode {
    AstNode *expr;      // expression to order by
    bool descending;    // true if 'desc' or 'descending'
} AstOrderSpec;

typedef struct AstGroupKey : AstNode {
    AstNode *expr;      // grouping key expression
    String* alias;      // attribute name on the group element
} AstGroupKey;

// Group clause: group by expr [as alias], ... into name
typedef struct AstGroupClause : AstNode {
    AstGroupKey *keys;  // linked list of key specs
    String* name;       // group binding name (from 'into name')
    int key_count;
} AstGroupClause;

typedef struct AstForNode : AstNode {
    AstNode *loop;       // loop bindings (linked list of AstLoopNode)
    AstNode *let_clause; // let bindings (linked list of AstNamedNode)
    AstNode *where;      // where condition (single expression, or NULL)
    AstGroupClause *group; // group by clause (or NULL)
    AstNode *order;      // order by specs (linked list of AstOrderSpec, or NULL)
    AstNode *limit;      // limit count expression (or NULL)
    bool limit_from_end;  // true for `limit last N`
    AstNode *offset;     // offset count expression (or NULL)
    AstNode *then;       // body expression
    NameScope *vars;     // scope for the variables in the loop
} AstForNode;

typedef struct AstListNode : AstArrayNode {
    AstNode *declare;  // declarations in the list
    NameScope *vars;  // scope for the variables in the list
    TypeList* list_type;
} AstListNode;

typedef struct AstElementNode : AstMapNode {
    AstNode *content;  // first content node
} AstElementNode;

// ==================== String/Symbol Pattern AST Nodes ====================

// Pattern definition node (string name = pattern OR symbol name = pattern)
// Extends AstNamedNode so it has 'name' and 'as' (the pattern expression)
typedef struct AstPatternDefNode : AstNamedNode {
    bool is_symbol;     // true for symbol pattern, false for string pattern
} AstPatternDefNode;

// Inline delimited pattern type, including the domain tag carried by its opener.
typedef struct AstPatternIslandNode : AstNode {
    AstNode* pattern;   // parsed content-language AST
    bool is_symbol;     // true only for the tagged \\symbol(...) opener
    int pattern_index;  // module-local compiled TypePattern index
} AstPatternIslandNode;

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

// View/Edit template node
// view [name:] pattern [(params)] [return_type] [state k:v, ...] { body } [on event() { ... }]*
typedef struct AstViewNode : AstNode {
    String* name;               // optional template name (NULL for anonymous)
    bool is_edit;               // true for 'edit', false for 'view'
    AstNode* pattern;           // model pattern (type expression)
    AstNamedNode* param;        // optional parameters (linked list)
    AstNode* body;              // functional body
    struct AstStateEntry* state; // optional state declarations (linked list)
    struct AstEventHandler* handler; // optional event handlers (linked list)
    NameScope* vars;            // scope for params and state
} AstViewNode;

// State entry: name: initial_value
typedef struct AstStateEntry : AstNode {
    String* name;               // state variable name
    AstNode* value;             // initial value expression
    struct AstStateEntry* next_state; // next state entry in list
} AstStateEntry;

// Event handler: on event_name(param) { body }
typedef struct AstEventHandler : AstNode {
    String* event;              // event name (e.g., "click", "init")
    AstNamedNode* param;        // optional event parameter
    AstNode* body;              // procedural body
    NameScope* vars;            // handler scope
    struct AstEventHandler* next_handler; // next handler in list
} AstEventHandler;

// Literal spellings that carry no const-pool entry are re-read from source at
// use. Shared by MIR lowering and the T0 interpreter so both decode the same
// bytes to the same value.
static inline int64_t parse_int_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    int end = ts_node_end_byte(node);
    const char* text = source + start;
    int len = end - start;

    // Copy to null-terminated buffer
    char buf[128];
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';

    // Handle hex (0x), octal (0o), binary (0b)
    if (len > 2 && buf[0] == '0') {
        if (buf[1] == 'x' || buf[1] == 'X') return strtoll(buf, NULL, 16);
        if (buf[1] == 'o' || buf[1] == 'O') return strtoll(buf + 2, NULL, 8);
        if (buf[1] == 'b' || buf[1] == 'B') return strtoll(buf + 2, NULL, 2);
    }

    // Remove underscores (1_000_000 -> 1000000)
    char clean[128];
    int ci = 0;
    for (int i = 0; i < len && ci < (int)sizeof(clean) - 1; i++) {
        if (buf[i] != '_') clean[ci++] = buf[i];
    }
    clean[ci] = '\0';

    // C16 ruling 9: an integer-spelled literal may carry a non-negative
    // exponent (`10e1` is int 100), and the grammar tokenizes that as an
    // integer. strtoll stops at the 'e', so apply the exponent here. The
    // frontend has already rejected anything outside the ingestion band, so
    // this cannot overflow.
    char* endptr = NULL;
    int64_t value = strtoll(clean, &endptr, 10);
    if (endptr && (*endptr == 'e' || *endptr == 'E')) {
        const char* exp = endptr + 1;
        if (*exp == '+') exp++;
        long power = strtol(exp, NULL, 10);
        for (long i = 0; i < power; i++) value *= 10;
    }
    return value;
}

static inline bool parse_bool_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    return source[start] == 't';
}

// A type node's runtime identity is not always its TypeId: `date`/`time` share
// LMD_TYPE_DTIME, `list`/`number`/`integer` have no runtime TypeId of their own,
// and every sized numeric shares LMD_TYPE_NUM_SIZED. Those denote one specific
// TypeType singleton; anything else is base_type(tid). Shared so both tiers give
// `is`/`query` the same type identity.
//
// Returns the singleton when one applies, else NULL with *out_tid set to the
// TypeId the caller should pass to base_type().
static inline TypeType* lambda_type_node_singleton(Type* node_type, TypeId* out_tid) {
    TypeId tid = node_type ? node_type->type_id : LMD_TYPE_ANY;
    if (node_type && node_type->type_id == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)node_type;
        if (tt->type) {
            if (tt->type == &TYPE_DATE)    { if (out_tid) *out_tid = tid; return &LIT_TYPE_DATE; }
            if (tt->type == &TYPE_TIME)    { if (out_tid) *out_tid = tid; return &LIT_TYPE_TIME; }
            // 'list' never matches at runtime (LMD_TYPE_LIST no longer exists),
            // so fn_is must see the singleton rather than a base_type lookup.
            if (tt->type == &TYPE_LIST)    { if (out_tid) *out_tid = tid; return &LIT_TYPE_LIST; }
            if (tt->type == &TYPE_NUMBER)  { if (out_tid) *out_tid = tid; return &LIT_TYPE_NUMBER; }
            if (tt->type == &TYPE_INTEGER) { if (out_tid) *out_tid = tid; return &LIT_TYPE_INTEGER; }
            if (tt->type->type_id == LMD_TYPE_NUM_SIZED) {
                switch ((NumSizedType)tt->type->kind) {
                case NUM_INT8:    if (out_tid) *out_tid = tid; return &LIT_TYPE_I8;
                case NUM_INT16:   if (out_tid) *out_tid = tid; return &LIT_TYPE_I16;
                case NUM_INT32:   if (out_tid) *out_tid = tid; return &LIT_TYPE_I32;
                case NUM_UINT8:   if (out_tid) *out_tid = tid; return &LIT_TYPE_U8;
                case NUM_UINT16:  if (out_tid) *out_tid = tid; return &LIT_TYPE_U16;
                case NUM_UINT32:  if (out_tid) *out_tid = tid; return &LIT_TYPE_U32;
                case NUM_FLOAT16: if (out_tid) *out_tid = tid; return &LIT_TYPE_F16;
                case NUM_FLOAT32: if (out_tid) *out_tid = tid; return &LIT_TYPE_F32;
                default: break;
                }
            }
            tid = tt->type->type_id;
        }
    }
    if (out_tid) *out_tid = tid;
    return NULL;
}

// Content-list item classification, shared by MIR lowering and the T0
// interpreter so both split a content block into declarations, side-effect
// statements, and value expressions the same way.
static inline bool is_declaration_node(int node_type) {
    switch (node_type) {
    case AST_NODE_LET_STAM: case AST_NODE_PUB_STAM:
    case AST_NODE_TYPE_STAM: case AST_NODE_VAR_STAM:
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_VIEW:
        return true;
    default:
        return false;
    }
}

// Procedural side-effect statements: they execute but contribute no output
// value. IF_EXPR / WHILE_STAM / FOR_STAM can appear in functional code and do
// produce values, so they are deliberately absent.
static inline bool is_side_effect_stam(int node_type) {
    switch (node_type) {
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
    case AST_NODE_HANDLER_STAM:
        return true;
    default:
        return false;
    }
}

// A control-flow node in a content block that is not the block's value
// expression runs for its side effects only. Shared so both tiers decide
// "does this `for` / `if` / `while` contribute a value here?" identically.
static inline bool is_proc_flow_side_effect_node(AstNode* node, AstNode* last_value) {
    return node && node != last_value &&
           (node->node_type == AST_NODE_IF_EXPR ||
            node->node_type == AST_NODE_WHILE_STAM ||
            node->node_type == AST_NODE_FOR_STAM);
}

typedef Item (*main_func_t)(Context*);
typedef struct MIR_context *MIR_context_t;

// Script extends Input to inherit unified memory management
struct Script : Input {
    const char* reference;      // path (relative to the main script) and name of the script
    const char* directory;      // directory containing this script (for relative imports)
    int index;                  // index of the script in the runtime scripts list
    uint32_t module_state_id;   // stable shared module-state identity, independent of list position
    bool is_main;               // true if this is the main entry-point script
    bool is_loading;            // true while script is being loaded (for circular import detection)
    bool cache_retain;          // true when an imported module may survive per-script teardown
    bool cache_retired;         // true when a retained cache slot has been invalidated
    bool cache_cross_lang_tainted;  // true when the import subtree contains a cross-language module
    const char* source;
    LangProfile* profile;       // dormant Phase-1 language profile hook table
    time_t src_mtime;           // file timestamp captured when loaded
    off_t src_size;             // file size captured when loaded
    TSTree* syntax_tree;

    // AST-specific fields (beyond Input)
    AstNode *ast_root;
    AstIndex ast_index;          // one dense identity/index table for all post-CST passes
    NameScope* current_scope;   // current name scope
    ArrayList* const_list;      // list of constants (Script-specific)

    // JIT compilation (Script-specific)
    MIR_context_t jit_context;
    main_func_t main_func;      // transpiled main function
    bool mir_gen_initialized;   // whether this context initialized MIR_gen
    mpd_context_t* decimal_ctx;  // libmpdec context for decimal operations

    // Debug info for stack traces (function address → source mapping)
    ArrayList* debug_info;      // list of FuncDebugInfo*

    // Function name mapping: MIR internal name → Lambda human-readable name
    // Used by build_debug_info_table() to get user-friendly names
    struct hashmap* func_name_map;  // maps char* (MIR name) → char* (Lambda name)

    ArrayList* direct_imports;  // direct Lambda import dependencies, populated by MIR cache phases

    // ---- T0 AST interpreter (D8.1.1v2) ----
    // Module top level owns a frame plan just like a function does; its
    // module-level bindings live in a persistent-rooted slab rather than in
    // per-activation slots (AI6/D7.2.1).
    FnFramePlan interp_plan;
    Item* interp_slab;              // persistent-rooted module binding storage
    uint32_t interp_slab_count;     // Item lanes in interp_slab (tails follow)
    bool interp_planned;            // frame-plan pass has run for this Script
    bool interp_supported;          // pre-scan found only P0/P1-covered kinds
    AstNodeType interp_reject_kind; // first unsupported kind, for the log line
};

typedef struct Runtime Runtime;
struct LambdaError;  // forward declaration

// Namespace entry for tracking namespace declarations
typedef struct NamespaceEntry {
    String* prefix;            // namespace prefix (e.g., "svg", "xlink")
    Target* target;            // resolved namespace target (URL or path)
    struct NamespaceEntry* next;
} NamespaceEntry;

typedef struct Transpiler : Script {
    TSParser* parser;
    Runtime* runtime;

    // Error tracking for accumulated type errors
    int error_count;           // accumulated error count
    int max_errors;            // threshold (default: 10)
    ArrayList* errors;         // list of LambdaError* (structured errors)

    // relaxed mode (--static-warning): semantic (E2xx) type errors are
    // recorded here as warnings instead of errors, and compilation proceeds.
    // Parse/syntax errors are never downgraded. [SI3v2/TI6 per-surface policy]
    bool static_warning;       // downgrade semantic errors to warnings
    int warning_count;         // accumulated downgraded-warning count
    ArrayList* warnings;       // list of LambdaError* (downgraded diagnostics)

    // AST build recursion-depth guard — caps build_expr nesting so a pathologically
    // deep source (thousands of nested parens/brackets) reports an error instead of
    // overflowing the stack. Zeroed by the memset that initializes the Transpiler.
    int build_depth;

    // ANY-census: per-reason counts of expressions whose static type fell back
    // to `any` [Type_Infer TI3]. Read by the compile-time report and the AST
    // dump; carries no semantics, so an unrecorded site is a bookkeeping bug,
    // never a behavior change.
    int any_census[ANY_REASON_COUNT];

    // Namespace declarations (file-local)
    NamespaceEntry* namespaces;  // linked list of declared namespaces

    // Closure transpilation context
    AstFuncNode* current_closure;  // non-null when transpiling inside a closure body

    // Assignment name context (for naming anonymous closures)
    String* current_assign_name;  // name of variable being assigned (e.g., "level1" for let level1 = fn...)

    // Tail Call Optimization context
    AstFuncNode* tco_func;     // non-null when transpiling body of a TCO-enabled function
    bool in_tail_position;     // true when current expression is in tail position

    // Pipe injection context (for data | func(args) -> func(data, args))
    int pipe_inject_args;      // extra args to add when looking up sys_func (0 normally, 1 in pipe context)

    // Current function being transpiled (for proc return type checking)
    AstFuncNode* current_func_node;

    // MIR JIT workaround: track while loop nesting depth
    // When > 0, native variable assignments use *(&x)=v pattern to prevent
    // MIR optimizer from mishandling SSA destruction of swap patterns
    int while_depth;

    // unique counter for temporary variables (e.g., error propagation temps)
    int temp_var_counter;

    // A bare `^`/`^.`/`^[...]` is valid only while building a handler body.
    bool building_handler_body;

    // 'that' clause context: when true, bare identifiers not found in scope
    // are rewritten to ~.name (member access on current item)
    bool in_that_clause;

    // `last` is legal only while building a subscript field; lowering uses the
    // current object to turn it into len(object) - 1.
    int subscript_depth;
    AstNode* last_index_object;

    // Object method transpilation context
    AstObjectTypeNode* method_owner;  // non-null when transpiling a method body
    struct TypeObject* pn_method_obj_type;  // non-null inside pn method body (for field write-back)

    // While-loop cross-dependency analysis: tracks which variables need _store_i64
    // due to cross-variable read dependencies (lost-copy SSA bug workaround).
    // Variables only reading themselves (self-update like q = q + 1) are safe for
    // direct assignment; variables read by other assignments (swap patterns) are unsafe.
    String** loop_unsafe_vars;  // array of variable names that need _store_i64
    int loop_unsafe_count;      // number of unsafe variables

    // Variadic function body context: when true, return/raise must emit restore_vargs
    bool in_variadic_body;

    // Built-in module global imports: when true, functions from this module
    // can be called without prefix (e.g., `import math;` allows `sqrt(x)`)
    bool builtin_import_math;
    bool builtin_import_io;

    // Built-in module aliased imports: non-null when module imported with alias
    // (e.g., `import m:math;` sets builtin_alias_math to "m")
    String* builtin_alias_math;
    String* builtin_alias_io;

    // Descriptor-backed Jube module imports (e.g., `import radiant;`,
    // `import r:radiant;`). The compiler resolves these through JubeModuleDef.
    JubeModuleImport* jube_module_imports;
} Transpiler;

// Helper to check if arg_type is compatible with param_type
bool types_compatible(Type* arg_type, Type* param_type);

void format_binary_literal(StrBuf* strbuf, Binary* bin);
void print_root_item(StrBuf *strbuf, Item item, const char* indent="  ");
// for C to access
extern "C" void format_item(StrBuf *strbuf, Item item, int depth, const char* indent);

// for debugging onnly
void log_item(Item item, const char* msg="");
