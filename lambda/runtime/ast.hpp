#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "../../lib/mempool.h"

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

// Resolve the AST form that carries a constrained type predicate.  A named
// type is represented by its declaration binding, not by an evaluator-local
// copy, so both direct `int that …` and `x is Positive` retain the same
// predicate node (S11.4.6).
static inline AstConstrainedTypeNode* ast_constrained_type_node(AstNode* node) {
    node = ast_unwrap_primary(node);
    if (!node) return NULL;
    if (node->node_type == AST_NODE_CONSTRAINED_TYPE) {
        return (AstConstrainedTypeNode*)node;
    }
    if (node->node_type != AST_NODE_IDENT) return NULL;
    AstIdentNode* ident = (AstIdentNode*)node;
    AstNode* declaration = ident->entry ? ident->entry->node : NULL;
    if (!declaration || declaration->node_type != AST_NODE_ASSIGN ||
            !((AstNamedNode*)declaration)->is_type_definition) return NULL;
    AstNode* value = ast_unwrap_primary(((AstNamedNode*)declaration)->as);
    return value && value->node_type == AST_NODE_CONSTRAINED_TYPE
        ? (AstConstrainedTypeNode*)value : NULL;
}

// direct Lambda calls share their target and argument layout between MIR and
// T0. Keeping the lookup here prevents either tier from silently treating a
// named operand as positional when a call has a static definition.
static inline AstFuncNode* ast_direct_call_function(AstCallNode* call) {
    AstNode* function = call ? ast_unwrap_primary(call->function) : NULL;
    if (!function || function->node_type != AST_NODE_IDENT) return NULL;
    NameEntry* entry = ((AstIdentNode*)function)->entry;
    AstNode* node = entry ? entry->node : NULL;
    if (!node || (node->node_type != AST_NODE_FUNC &&
            node->node_type != AST_NODE_FUNC_EXPR &&
            node->node_type != AST_NODE_PROC)) return NULL;
    return (AstFuncNode*)node;
}

static inline bool ast_call_has_named_args(const AstCallNode* call) {
    for (AstNode* arg = call ? call->argument : NULL; arg; arg = arg->next) {
        if (arg->node_type == AST_NODE_NAMED_ARG) return true;
    }
    return false;
}

static inline void ast_resolve_call_args(AstNode* arg_list, AstFuncNode* fn_node,
        int arg_count, AstNode** resolved_args) {
    if (!resolved_args) return;
    bool has_named_args = false;
    AstNode* arg = arg_list;
    while (arg) {
        if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
        arg = arg->next;
    }

    if (has_named_args && fn_node) {
        int positional_idx = 0;
        for (arg = arg_list; arg; arg = arg->next) {
            if (arg->node_type == AST_NODE_NAMED_ARG) {
                AstNamedNode* named_arg = (AstNamedNode*)arg;
                int param_idx = 0;
                for (AstNamedNode* param = fn_node->param; param;
                        param = (AstNamedNode*)((AstNode*)param)->next, param_idx++) {
                    if (param->name && named_arg->name &&
                            param->name->len == named_arg->name->len &&
                            memcmp(param->name->chars, named_arg->name->chars,
                                param->name->len) == 0) {
                        if (param_idx < LAMBDA_MAX_FUNCTION_ARGS) {
                            resolved_args[param_idx] = named_arg->as;
                        }
                        break;
                    }
                }
            } else {
                while (positional_idx < LAMBDA_MAX_FUNCTION_ARGS &&
                        resolved_args[positional_idx]) {
                    positional_idx++;
                }
                if (positional_idx < LAMBDA_MAX_FUNCTION_ARGS) {
                    resolved_args[positional_idx++] = arg;
                }
            }
        }
        return;
    }

    arg = arg_list;
    for (int i = 0; i < arg_count && i < LAMBDA_MAX_FUNCTION_ARGS; i++) {
        resolved_args[i] = arg;
        arg = arg->next;
    }
}

// A `var` parameter borrows the caller's binding rather than receiving a
// value copy. Direct T0 calls use these resolved entries to publish the
// callee's replacement root back into that exact caller slot on return.
static inline bool ast_type_func_has_var_parameter(const TypeFunc* signature) {
    if (!signature || signature->type_id != LMD_TYPE_FUNC) return false;
    for (const TypeParam* param = signature->param; param; param = param->next) {
        if (param->is_var_param) return true;
    }
    return false;
}

// `any[]` is still a declaration contract, but its element boundary accepts
// every Item. T0's ordinary COW setter therefore supplies the full contract;
// only a narrower element type needs the checked-store runtime entry.
static inline Type* ast_declared_array_element(Type* declared) {
    if (!declared) return NULL;
    if (declared->type_id == LMD_TYPE_TYPE &&
            declared->kind == TYPE_KIND_UNARY &&
            ((TypeUnary*)declared)->op == OPERATOR_REPEAT) {
        return type_field_unwrap_simple_decl(((TypeUnary*)declared)->operand);
    }
    Type* semantic = type_field_unwrap_simple_decl(declared);
    if (!semantic || semantic->type_id != LMD_TYPE_ARRAY || semantic == &TYPE_LIST) {
        return NULL;
    }
    TypeArray* array = (TypeArray*)semantic;
    return !array->item_patterns && array->nested
        ? type_field_unwrap_simple_decl(array->nested) : NULL;
}

static inline bool ast_declared_type_is_open_any_array(Type* declared) {
    Type* element = ast_declared_array_element(declared);
    return element && element->type_id == LMD_TYPE_ANY;
}

static inline bool ast_declared_type_is_map(Type* declared) {
    Type* semantic = type_field_unwrap_simple_decl(declared);
    return semantic && semantic->type_id == LMD_TYPE_MAP;
}

// Object fields take precedence over methods at member lookup time. Keep the
// same table walk available to both T0 admission and execution so a callable
// field cannot be mistaken for a bound method.
static inline TypeMethod* ast_lookup_object_method(TypeObject* object,
        const String* name) {
    if (!object || !name) return NULL;
    for (TypeObject* owner = object; owner; owner = owner->base) {
        for (ShapeEntry* field = owner->shape; field; field = field->next) {
            if (field->name && field->name->length == name->len &&
                    memcmp(field->name->str, name->chars, name->len) == 0) {
                return NULL;
            }
        }
    }
    for (TypeObject* owner = object; owner; owner = owner->base) {
        for (TypeMethod* method = owner->methods; method; method = method->next) {
            if (method->name && method->name->length == name->len &&
                    memcmp(method->name->str, name->chars, name->len) == 0) {
                return method;
            }
        }
    }
    return NULL;
}

// A top-level `any` contract carries no narrower occurrence or element
// invariant. It therefore needs the ordinary runtime COW setter, unlike a
// declared map/array contract that must route through its checked setter.
static inline bool ast_declared_type_is_open_item(Type* declared) {
    Type* semantic = type_field_unwrap_simple_decl(declared);
    if (!semantic) return false;
    if (semantic->type_id == LMD_TYPE_ANY) return true;
    // `any | error` is normalized to the same open value contract at its
    // mutation boundary, but it may still retain its union graph in the AST.
    if (!lambda_type_is_union(semantic)) return false;
    TypeBinary* binary = (TypeBinary*)semantic;
    Type* left = type_field_unwrap_simple_decl(binary->left);
    Type* right = type_field_unwrap_simple_decl(binary->right);
    return (left && left->type_id == LMD_TYPE_ANY) ||
        (right && right->type_id == LMD_TYPE_ANY);
}

static inline bool ast_direct_call_var_parameter_entries(AstCallNode* call,
        const TypeFunc* signature, NameEntry** entries) {
    AstFuncNode* target = ast_direct_call_function(call);
    if (!target || !signature || !entries || signature->is_variadic ||
            signature->param_count < 0 ||
            signature->param_count > LAMBDA_MAX_FUNCTION_ARGS ||
            signature->required_param_count != signature->param_count) {
        return false;
    }

    int source_count = 0;
    for (AstNode* arg = call->argument; arg; arg = arg->next) source_count++;
    if (source_count != signature->param_count) return false;

    AstNode* resolved[LAMBDA_MAX_FUNCTION_ARGS] = {0};
    ast_resolve_call_args(call->argument, target, source_count, resolved);
    const TypeParam* param = signature->param;
    for (int index = 0; index < signature->param_count; index++, param = param->next) {
        entries[index] = NULL;
        if (!param || !param->is_var_param) continue;
        AstNode* argument = ast_unwrap_primary(resolved[index]);
        if (!argument || argument->node_type != AST_NODE_IDENT) return false;
        NameEntry* entry = ((AstIdentNode*)argument)->entry;
        if (!entry || entry->import) return false;
        for (int prior = 0; prior < index; prior++) {
            if (entries[prior] == entry) return false;
        }
        entries[index] = entry;
    }
    return true;
}

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
    NameEntry* entry;   // post-group binding for `into name`
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
static inline int64_t parse_int_literal_span(const char* source, SourceSpan span) {
    const char* text = source + span.start_byte;
    int len = (int)lambda_source_span_length(span);

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

static inline bool parse_bool_literal_span(const char* source, SourceSpan span) {
    return source[span.start_byte] == 't';
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
    case AST_NODE_DECOMPOSE:
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

// S16.6.8: the pn-only constructs whose presence at a block's TOP LEVEL makes
// the block a statement rather than an expression. Deliberately narrower than
// `is_side_effect_stam`: `raise` is an expression (fn-land divergence) and a
// handler statement is not pn-only, so neither disqualifies a block from value
// position. `while` is included because S16.6.5 makes it procedural-only, so a
// block containing one can never be a functional value.
static inline bool is_procedural_only_stam(int node_type) {
    switch (node_type) {
    case AST_NODE_RETURN_STAM:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_WHILE_STAM:
        return true;
    default:
        return false;
    }
}

// S16.6.8/S16.6.9 branch classification. Three-way, because an EMPTY braced
// branch (`} else if (c) { } else {`) commits to neither side and must pair
// with either — treating it as a value branch rejected working procedural code.
enum AstBranchKind { AST_BRANCH_NEUTRAL = 0, AST_BRANCH_VALUE, AST_BRANCH_CONTROL };

AstBranchKind ast_branch_kind(AstNode* node);

// S16.6.8: a braced block is a STATEMENT, never an expression, when its
// interior is procedural. Classification is by interior, extending S16.4.1v2's
// doctrine from map-vs-block to statement-ness — so a functional block
// (`{ let r = f(x); g(r) }`) stays an expression everywhere, including after
// `case T:` and as an `=>` arrow body.
static inline bool ast_block_is_procedural(AstNode* node) {
    return node && node->node_type == AST_NODE_CONTENT &&
        ast_branch_kind(node) == AST_BRANCH_CONTROL;
}

// A can-raise procedural body must preserve failures from these discarded
// statements. Both execution tiers use this predicate before throwing away a
// statement result, so an OOB store cannot become a successful no-op (S7.1.1).
static inline bool side_effect_result_can_error(int node_type) {
    switch (node_type) {
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
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
struct Transpiler;

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
    // AST-specific fields (beyond Input)
    AstNode *ast_root;
    AstIndex ast_index;          // one dense identity/index table for all post-parse passes
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
    uint32_t interp_satellite_count; // unique MIR satellite image sequence
    bool interp_views_registered;   // T0 view entries published in this context

    // The REPL keeps its append-only source buffer alive because AST source
    // spans point into it. `source` aliases repl_source->str in that mode.
    StrBuf* repl_source;
    // Append cursor makes retained REPL history O(fragment) to extend rather
    // than re-walking every prior top-level node on each completed input.
    AstNode* repl_last_top_level;
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
    // The Script that will retain this AST after the stack-local transpiler is
    // adopted. Object methods need this stable owner for their interpreter
    // closures; `(Script*)this` is valid only during construction.
    Script* script_owner;
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

    // AST build recursion-depth guard — caps reduction nesting so a pathologically
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
