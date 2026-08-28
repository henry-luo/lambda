#pragma once

#include <stdint.h>
#include "source_span.h"
#include "../lambda.h"
#include "value_rep.h"

typedef struct NamePool NamePool;
typedef struct Script Script;
typedef struct AstNode AstNode;
typedef struct AstImportNode AstImportNode;
typedef struct NameEntry NameEntry;
typedef struct NameScope NameScope;
typedef struct LangProfile LangProfile;
typedef struct TsTypeAnnotationNode TsTypeAnnotationNode;
typedef struct _ArrayList ArrayList;

typedef enum AstNodeType : uint16_t {
    AST_NODE_NULL = 0,

    // L0 roots
    AST_SCRIPT = 1,

    // L1 expressions and expression-like leaves
    AST_NODE_PRIMARY = 50,
    AST_NODE_LITERAL = 51,
    AST_NODE_IDENT = 52,
    AST_NODE_UNARY = 53,
    AST_NODE_SPREAD = 54,
    AST_NODE_BINARY = 55,
    AST_NODE_ASSIGN = 56,
    AST_NODE_CALL_EXPR = 57,
    AST_NODE_MEMBER_EXPR = 58,
    AST_NODE_INDEX_EXPR = 59,
    AST_NODE_IF_EXPR = 60,
    AST_NODE_ARRAY = 61,
    AST_NODE_MAP = 62,
    AST_NODE_KEY_EXPR = 63,
    AST_NODE_MATCH_EXPR = 64,
    AST_NODE_MATCH_ARM = 65,
    AST_NODE_NEW_EXPR = 66,
    AST_NODE_SEQ = 67,
    AST_NODE_LIST = 68,
    AST_NODE_BLOCK = 69,
    AST_NODE_EXPR_STMT = 70,
    AST_NODE_PROPERTY = 71,
    AST_NODE_CONDITIONAL_EXPR = 72,

    // L2/L3 declarations, statements, patterns, and control flow
    AST_NODE_PARAM = 150,
    AST_NODE_FOR_STAM = 151,
    AST_NODE_WHILE_STAM = 152,
    AST_NODE_BREAK_STAM = 153,
    AST_NODE_CONTINUE_STAM = 154,
    AST_NODE_RETURN_STAM = 155,
    AST_NODE_RAISE_STAM = 156,
    AST_NODE_RAISE_EXPR = 157,
    AST_NODE_VAR_STAM = 158,
    AST_NODE_ASSIGN_STAM = 159,
    AST_NODE_LET_STAM = 160,
    AST_NODE_PUB_STAM = 161,
    AST_NODE_IMPORT = 162,
    AST_NODE_EXPORT = 163,
    AST_NODE_YIELD = 164,
    AST_NODE_AWAIT = 165,
    AST_NODE_ASSIGN_PATTERN = 166,
    AST_NODE_ARRAY_PATTERN = 167,
    AST_NODE_MAP_PATTERN = 168,
    AST_NODE_REST_ELEMENT = 169,
    AST_NODE_REST_PROPERTY = 170,
    AST_NODE_VARIABLE_DECLARATOR = 171,
    AST_NODE_DO_WHILE_STAM = 172,
    AST_NODE_FOR_OF_STAM = 173,
    AST_NODE_FOR_IN_STAM = 174,
    AST_NODE_TRY_STAM = 175,
    AST_NODE_CATCH_CLAUSE = 176,

    // L4 callable forms
    AST_NODE_FUNC = 300,
    AST_NODE_FUNC_EXPR = 301,
    AST_NODE_PROC = 302,
    AST_NODE_ARROW_FUNC = 303,
    AST_NODE_METHOD = 304,

    // L5/L6 type/module/class forms that are not yet structurally unified.
    AST_NODE_CLASS = 350,
    AST_NODE_FIELD = 351,
    AST_NODE_CLASS_EXPR = 352,
    AST_NODE_IMPORT_SPECIFIER = 400,
    AST_NODE_EXPORT_SPECIFIER = 401,

    // Lambda-specific range
    AST_NODE_PIPE = 500,
    AST_NODE_CURRENT_ITEM = 501,
    AST_NODE_CURRENT_INDEX = 502,
    AST_NODE_LAST_INDEX = 503,
    AST_NODE_CONTENT = 504,
    AST_NODE_ELEMENT = 505,
    AST_NODE_DECOMPOSE = 506,
    AST_NODE_LOOP = 507,
    AST_NODE_ORDER_SPEC = 508,
    AST_NODE_GROUP_CLAUSE = 509,
    AST_NODE_JOIN_KEY = 510,
    AST_NODE_FOR_EXPR = 511,
    AST_NODE_INDEX_ASSIGN_STAM = 512,
    AST_NODE_MEMBER_ASSIGN_STAM = 513,
    AST_NODE_PIPE_FILE_STAM = 514,
    AST_NODE_TYPE_STAM = 515,
    AST_NODE_PATH_EXPR = 516,
    AST_NODE_PATH_INDEX_EXPR = 517,
    AST_NODE_NAVIGATION_EXPR = 518,
    AST_NODE_QUERY_EXPR = 519,
    AST_NODE_SYS_FUNC = 520,
    AST_NODE_NAMED_ARG = 521,
    AST_NODE_TYPE = 522,
    AST_NODE_CONTENT_TYPE = 523,
    AST_NODE_LIST_TYPE = 524,
    AST_NODE_ARRAY_TYPE = 525,
    AST_NODE_MAP_TYPE = 526,
    AST_NODE_ELMT_TYPE = 527,
    AST_NODE_FUNC_TYPE = 528,
    AST_NODE_BINARY_TYPE = 529,
    AST_NODE_UNARY_TYPE = 530,
    AST_NODE_CONSTRAINED_TYPE = 531,
    AST_NODE_OBJECT_TYPE = 532,
    AST_NODE_OBJECT_LITERAL = 533,
    AST_NODE_STRING_PATTERN = 534,
    AST_NODE_SYMBOL_PATTERN = 535,
    AST_NODE_PATTERN_RANGE = 536,
    AST_NODE_PATTERN_CHAR_CLASS = 537,
    AST_NODE_PATTERN_SEQ = 538,
    AST_NODE_VIEW = 539,
    AST_NODE_STATE_ENTRY = 540,
    AST_NODE_START = 541,
    // 542, not 541: AST_NODE_START accidentally reused the event-handler value,
    // making the two node kinds indistinguishable in node_type dispatch.
    AST_NODE_EVENT_HANDLER = 542,
    AST_NODE_HANDLER_EXPR = 543,
    AST_NODE_HANDLER_STAM = 544,
    // `^` is the current error value only while building/transpiling a
    // braced error-handler body; it is distinct from `~` current-item state.
    AST_NODE_CURRENT_ERROR = 545,
    AST_NODE_PATTERN_ISLAND = 546,
    // A group clause owns a linked list of these; keeping the key distinct
    // prevents generic AST visitors from casting its smaller layout as the
    // enclosing AstGroupClause.
    AST_NODE_GROUP_KEY = 547,
} AstNodeType;

typedef enum Operator {
    // unary
    OPERATOR_NOT,
    OPERATOR_NEG,
    OPERATOR_POS,
    OPERATOR_SPREAD,
    OPERATOR_PROPAGATE,

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
    OPERATOR_ELEM_EQ,
    OPERATOR_ELEM_NE,
    OPERATOR_ELEM_LT,
    OPERATOR_ELEM_LE,
    OPERATOR_ELEM_GT,
    OPERATOR_ELEM_GE,

    OPERATOR_TO,
    OPERATOR_UNION,
    OPERATOR_INTERSECT,
    OPERATOR_EXCLUDE,
    OPERATOR_IS,
    OPERATOR_IS_NAN,
    OPERATOR_IN,
    OPERATOR_AT,

    // pipe operators
    OPERATOR_PIPE,
    OPERATOR_WHERE,
    OPERATOR_PIPE_FILE,
    OPERATOR_PIPE_APPEND,

    // occurrence
    OPERATOR_OPTIONAL,
    OPERATOR_ONE_MORE,
    OPERATOR_ZERO_MORE,
    OPERATOR_REPEAT,

    OPERATOR_ASSIGN,

    // JavaScript-only operator superset, dormant for Lambda until JS adopts Operator.
    OPERATOR_JS_STRICT_EQ,
    OPERATOR_JS_STRICT_NE,
    OPERATOR_JS_EXP,
    OPERATOR_JS_BIT_AND,
    OPERATOR_JS_BIT_OR,
    OPERATOR_JS_BIT_XOR,
    OPERATOR_JS_LSHIFT,
    OPERATOR_JS_RSHIFT,
    OPERATOR_JS_URSHIFT,
    OPERATOR_JS_BIT_NOT,
    OPERATOR_JS_TYPEOF,
    OPERATOR_JS_VOID,
    OPERATOR_JS_DELETE,
    OPERATOR_JS_INCREMENT,
    OPERATOR_JS_DECREMENT,
    OPERATOR_JS_ADD_ASSIGN,
    OPERATOR_JS_SUB_ASSIGN,
    OPERATOR_JS_MUL_ASSIGN,
    OPERATOR_JS_DIV_ASSIGN,
    OPERATOR_JS_MOD_ASSIGN,
    OPERATOR_JS_EXP_ASSIGN,
    OPERATOR_JS_BIT_AND_ASSIGN,
    OPERATOR_JS_BIT_OR_ASSIGN,
    OPERATOR_JS_BIT_XOR_ASSIGN,
    OPERATOR_JS_LSHIFT_ASSIGN,
    OPERATOR_JS_RSHIFT_ASSIGN,
    OPERATOR_JS_URSHIFT_ASSIGN,
    OPERATOR_JS_INSTANCEOF,
    OPERATOR_JS_NULLISH_COALESCE,
    OPERATOR_JS_NULLISH_ASSIGN,
    OPERATOR_JS_AND_ASSIGN,
    OPERATOR_JS_OR_ASSIGN,
    // reserve frontend-specific operator tags as enum members so switch
    // labels remain constant expressions across Clang's Windows ABI.
    OPERATOR_PY_MATMUL = 3000,
    OPERATOR_PY_NOT_IN,
    OPERATOR_PY_IS_NOT,
    OPERATOR_PY_FLOOR_DIV_ASSIGN,
    OPERATOR_PY_MATMUL_ASSIGN,
} Operator;

typedef enum AstLiteralType {
    AST_LITERAL_NUMBER,
    AST_LITERAL_STRING,
    AST_LITERAL_BOOLEAN,
    AST_LITERAL_NULL,
    AST_LITERAL_UNDEFINED,
    // reserve Python-only literal tags in the shared AST domain.
    AST_LITERAL_PY_INT = 1000,
    AST_LITERAL_PY_FLOAT,
} AstLiteralType;

typedef enum ScopeKind {
    SCOPE_KIND_GLOBAL,
    SCOPE_KIND_MODULE,
    SCOPE_KIND_FUNCTION,
    SCOPE_KIND_BLOCK,
} ScopeKind;

// entry in the name_stack
struct NameEntry {
    String* name;
    AstNode* node;
    NameEntry* next;
    AstImportNode* import;
    NameScope* scope;
    // A sloppy block function publishes only to this validated Annex-B outer
    // binding; null records a parameter, arguments, or lexical collision.
    NameEntry* annex_b_outer_binding;
    bool is_mutable;
    bool is_var_param;
    bool is_parameter;
    // A plain (non-`var`) parameter of a `pn`. Under the current pn ABI such a
    // parameter is locally mutable AND its typed-container writes stay visible
    // to the caller, so a checked write must use the in-place setter rather
    // than validating a detached candidate. MIR Direct carries the same fact on
    // MirVarEntry::is_proc_param; T0 read only is_var_param and therefore
    // detached, which silently dropped the write and made typed json2 parse to
    // the wrong value on the interpreter tier while the JIT was correct.
    bool is_proc_param;
    bool has_type_annotation;
    // The explicit source annotation, when present.  `node->type` is the
    // effective compiler type and historically lost this distinction during
    // declaration construction, which let later boundaries guess from TypeId.
    Type* declared_type;
    bool type_widened;
    bool is_lexical;
    // loop-head bindings need a distinct capture-analysis fact; this used to
    // be recovered from parser structure at the binding node.
    bool is_for_in_head;
    bool is_const;
    bool tdz_active;
    bool is_exported;
    // T0 frame-plan facts (AI5). NameEntry is pool-calloc'd, so zero-init must
    // read as "no plan": slot 0 is a legal frame slot, hence the explicit flag
    // rather than a sentinel index.
    int32_t slot;
    BindingStorage binding_storage;
    bool storage_assigned;
    // Whether this binding holds a freshly-produced container, so an alias of
    // it is an ownership boundary that must share the root before a later write
    // (cow_bind_var). Both tiers decide it with the same rule; the JIT keeps the
    // equivalent on MirVarEntry::cow_owned.
    bool cow_owned;
    // When this name was hung into the scope by an import, the module that
    // actually declares it. `slot` is then an index into *that* module's slab,
    // not this one's — the two modules' plan passes number their globals
    // independently (§4.1: const/type/slab lookups go against the declaring
    // Script, not the current one).
    Script* import_owner;
};

// Static activation shape for one interpreted function (or module top level).
// Slot layout: [ params | locals | vargs? | signal | scratch ] — see interp.hpp.
typedef struct FnFramePlan {
    uint16_t param_count;
    uint16_t local_count;
    // A variadic function keeps its adapter-owned rest list rooted for the
    // whole body. UINT16_MAX means this frame has no variadic binding.
    uint16_t vargs_index;
    uint16_t scratch_depth;   // max Items live across a child eval / MAY_GC call
    uint16_t total_slots;     // params + locals + vargs? + 1 (signal) + scratch
    bool planned;
} FnFramePlan;

// name_scope
struct NameScope {
    NameEntry* first;
    NameEntry* last;
    bool is_proc;
    NameScope* parent;
    ScopeKind kind;
    bool strict;
    // Function bodies keep a block environment for top-level lexicals, while
    // their FunctionDeclarations still belong to the enclosing function scope.
    bool is_function_body;
    bool has_implicit_arguments;
    // JavaScript B.3.5 permits a legacy var redeclaration of a simple catch
    // BindingIdentifier; JavaScript marks only that handler scope.
    bool allows_legacy_var_redeclaration;
};

struct AstNode {
    AstNodeType node_type;
    Type *type;
    AstNode* next;
    SourceSpan source_span;
};

// Stable compiler identities and one authoritative child/index contract. The
// index is deliberately representation-neutral: language profiles add only
// extension children, while all core passes use these dense IDs.
typedef uint32_t AstNodeId;
typedef uint32_t AstFunctionId;
#define AST_NODE_ID_INVALID UINT32_MAX
#define AST_FUNCTION_ID_INVALID UINT32_MAX

typedef void (*AstChildVisitor)(AstNode* child, AstNode* parent, void* ctx);

typedef struct AstIndex {
    AstNode** nodes;
    AstNode** parents;
    AstFunctionId* owner_functions;
    // Dense function roots make FunctionId the shared authority for Lambda
    // and JS lowering instead of requiring each frontend to rescan nodes.
    AstNode** functions;
    struct AstNodeFacts* facts;
    uint32_t count;
    uint32_t capacity;
    uint32_t function_count;
    uint32_t function_capacity;
    AstNode** slots;
    AstNodeId* slot_ids;
    uint32_t slot_capacity;
} AstIndex;

typedef struct AstNodeFacts {
    Type* declared_contract;
    Type* inferred_type;
    ValueRep representation;
    uint32_t flags;
    // const pass results are immediate Items only. Pointer-backed values stay
    // out of this table so an AST fact cannot become a MIR-cache relocation
    // dependency (D8.1.1v2 / DI14).
    uint64_t folded_item;
} AstNodeFacts;

enum AstNodeFactFlags : uint32_t {
    AST_NODE_FACT_NONE = 0,
    AST_NODE_FACT_CONST_FOLDED = 1u << 0,
};

#ifdef __cplusplus
extern "C" {
#endif
void ast_visit_core_children(AstNode* node, AstChildVisitor visitor, void* ctx);
bool ast_index_build(AstIndex* index, AstNode* root);
bool ast_index_build_profile(AstIndex* index, AstNode* root, const LangProfile* profile);
// Adds a newly retained AST fragment without invalidating the stable IDs and
// analysis facts already published for earlier REPL inputs (D8.2.4).
bool ast_index_append_profile(AstIndex* index, AstNode* root, AstNode* parent,
                              const LangProfile* profile);
void ast_index_destroy(AstIndex* index);
AstNodeId ast_index_find(const AstIndex* index, const AstNode* node);
#ifdef __cplusplus
}
#endif

typedef struct AstFieldNode : AstNode {
    AstNode *object;
    union {
        AstNode *field;
        AstNode *property;
    };
    bool computed;
    bool optional;
} AstFieldNode;

typedef struct AstCallNode : AstNode {
    union {
        AstNode *function;
        AstNode *callee;
    };
    union {
        AstNode *argument;
        AstNode *arguments;
    };
    bool pipe_inject;
    bool propagate;
    bool can_raise;
    bool optional;
    bool is_proc_method;
    // Marked by the T0 frame-plan pass when this call is a self-recursive call
    // in tail position. The walker then rebinds parameter slots and re-enters
    // the body instead of opening a frame, matching the loop lowering emits
    // for the same shape (AIO1's self-tail-call slice).
    bool interp_self_tail_call;
} AstCallNode;

// A handler keeps both outcome bodies together so expression and statement
// forms share the same single-evaluation routing semantics in the backend.
typedef struct AstHandlerNode : AstNode {
    AstNode* operand;
    AstNode* body;
    AstNode* value_body; // optional non-error arm, with `~` bound to the operand
    bool is_statement;
    // Async MIR reserves a dispatcher state for a statement pn handler's
    // fault-only continuation. Zero means that this handler is not a
    // suspension-capable procedural target.
    int async_fault_state;
} AstHandlerNode;

typedef enum StartMode {
    START_MODE_TASK = 0,
    START_MODE_THREAD,
    START_MODE_PROCESS,
} StartMode;

typedef struct AstStartNode : AstNode {
    AstCallNode* call;
    NameScope* owner_scope;
    StartMode mode;
    bool escapes;
} AstStartNode;

typedef struct AstPrimaryNode : AstNode {
    AstNode *expr;
} AstPrimaryNode;

typedef struct AstLiteralNode : AstPrimaryNode {
    AstLiteralType literal_type;
    bool has_decimal;
    bool is_bigint;
    union {
        double number_value;
        String* string_value;
        bool boolean_value;
    } value;
    String* bigint_str;
} AstLiteralNode;

typedef AstNode AstTypeNode;

typedef struct AstUnaryNode : AstNode {
    AstNode *operand;
    StrView op_str;
    Operator op;
    bool prefix;
} AstUnaryNode;

typedef struct AstBinaryNode : AstNode {
    AstNode *left, *right;
    StrView op_str;
    Operator op;
} AstBinaryNode;

typedef AstBinaryNode AstPipeNode;

// for AST_NODE_ASSIGN, AST_NODE_KEY_EXPR, AST_NODE_PARAM
typedef struct AstNamedNode : AstNode {
    String* name;
    AstNode *as;
    NameEntry* entry;
    bool is_type_definition;
    // Kept separately from AstNode::type so a declaration can retain both its
    // source annotation and its initializer's inferred type (`as->type`).
    Type* declared_type;
} AstNamedNode;

typedef struct AstIdentNode : AstNode {
    String* name;
    NameEntry *entry;
} AstIdentNode;

typedef struct AstLetNode : AstNode {
    AstNode *declare;
} AstLetNode;

typedef struct AstIfNode : AstNode {
    union {
        AstNode *cond;
        AstNode *test;
    };
    union {
        AstNode *then;
        AstNode *consequent;
    };
    union {
        AstNode *otherwise;
        AstNode *alternate;
    };
} AstIfNode;

typedef struct AstMatchArm : AstNode {
    union {
        AstNode *pattern;
        AstNode *test;
    };
    union {
        AstNode *body;
        AstNode *consequent;
    };
    // S16.6.8/S16.6.9: true for the `case T { ... }` spelling, false for
    // `case T: expr`. The two reduce to the same node, but only the colon
    // spelling bars a procedural block, and the distinction drives the
    // value-arm/control-arm reading.
    bool body_braced;
} AstMatchArm;

typedef struct AstMatchNode : AstNode {
    union {
        AstNode *scrutinee;
        AstNode *discriminant;
    };
    union {
        AstMatchArm *first_arm;
        AstNode *cases;
    };
    int arm_count;
} AstMatchNode;

typedef struct AstWhileNode : AstNode {
    union {
        AstNode *cond;
        AstNode *test;
    };
    AstNode *body;
    NameScope *vars;
} AstWhileNode;

typedef AstWhileNode AstDoWhileNode;

typedef struct AstForStmtNode : AstNode {
    AstNode* init;
    AstNode* test;
    AstNode* update;
    AstNode* body;
} AstForStmtNode;

typedef struct AstBreakContinueNode : AstNode {
    const char* label;
    int label_len;
} AstBreakContinueNode;

typedef struct AstReturnNode : AstNode {
    union {
        AstNode *value;
        AstNode *argument;
    };
} AstReturnNode;

typedef struct AstRaiseNode : AstNode {
    union {
        AstNode *value;
        AstNode *argument;
    };
} AstRaiseNode;

typedef struct AstArrayNode : AstNode {
    union {
        AstNode *item;
        AstNode *elements;
        AstNode *expressions;
    };
    int length;
} AstArrayNode;

typedef struct AstMapNode : AstNode {
    union {
        AstNode *item;
        AstNode *properties;
    };
} AstMapNode;

typedef struct AstPropertyNode : AstNode {
    AstNode* key;
    AstNode* value;
    bool computed;
    bool method;
    bool is_getter;
    bool is_setter;
    bool shorthand;
} AstPropertyNode;

typedef struct AstAssignNode : AstNode {
    Operator op;
    AstNode *left;
    AstNode *right;
    bool lhs_is_parenthesized;
} AstAssignNode;

// for AST_NODE_ASSIGN with decomposition (let a, b = expr / let a, b at expr)
typedef struct AstDecomposeNode : AstNode {
    String** names;
    // `names` is source-facing; T0 binds through these resolved entries so a
    // decomposed target uses the same planned slot as later identifier reads.
    NameEntry** entries;
    int name_count;
    AstNode *as;
    bool is_named;
} AstDecomposeNode;

// assignment statement (procedural only)
typedef struct AstAssignStamNode : AstAssignNode {
    String* target;
    AstNode *target_node;
    AstNode *value;
    struct NameEntry* target_entry;
} AstAssignStamNode;

// compound assignment statement: arr[i] = val or obj.field = val (procedural only)
typedef struct AstCompoundAssignNode : AstAssignNode {
    AstNode *object;
    AstNode *key;
    AstNode *value;
} AstCompoundAssignNode;

// A vector comparison produces an ArrayNum bool mask, while a source numeric
// literal may still carry the general ARRAY AST type until it is constructed.
// T0 shares this syntactic admission test between planning and execution; the
// runtime fn_index_assign helper still validates the actual ArrayNum lanes and
// shape before it mutates.
static inline bool ast_is_direct_numeric_mask_assignment(AstNode* node) {
    if (!node || node->node_type != AST_NODE_INDEX_ASSIGN_STAM) return false;
    AstCompoundAssignNode* assignment = (AstCompoundAssignNode*)node;
    AstNode* key = assignment->key;
    AstNode* object = assignment->object;
    return key && !key->next && key->type &&
        key->type->type_id == LMD_TYPE_ARRAY_NUM && object && object->type &&
        (object->type->type_id == LMD_TYPE_ARRAY ||
         object->type->type_id == LMD_TYPE_ARRAY_NUM);
}

// --- Shared AST shape helpers (promoted from transpile-mir.cpp, rule 13) ---
// Both tiers need the same answers about an assignment target, so the walks
// live here rather than being re-derived: the MIR lowering and the T0 walker
// call these same functions.

// `(expr)` wrappers carry no semantics; every consumer wants the inner node.
static inline AstNode* ast_unwrap_primary(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    return node;
}

static inline bool ast_type_needs_mutable_clone(TypeId type_id) {
    return type_id == LMD_TYPE_ANY || type_id == LMD_TYPE_ARRAY ||
           type_id == LMD_TYPE_ARRAY_NUM || type_id == LMD_TYPE_MAP ||
           type_id == LMD_TYPE_ELEMENT || type_id == LMD_TYPE_OBJECT;
}

// An assignment target decomposed into its root binding plus the field/index
// chain reaching the written slot. `count == 0` means the root itself is
// written (`a[i] = v`), which is the only shape the T0 walker handles.
enum { AST_COW_PATH_MAX = 32 };
typedef struct AstCowPath {
    AstNode* root;
    AstNode* segment[AST_COW_PATH_MAX];
    bool is_member[AST_COW_PATH_MAX];
    int count;
} AstCowPath;

static inline bool ast_collect_cow_path(AstCowPath* path, AstNode* node) {
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        path->root = node;
        return true;
    }
    if (node->node_type != AST_NODE_INDEX_EXPR && node->node_type != AST_NODE_MEMBER_EXPR) {
        return false;
    }
    AstFieldNode* field = (AstFieldNode*)node;
    if (!ast_collect_cow_path(path, field->object) || !field->field ||
            field->field->next || path->count >= AST_COW_PATH_MAX) {
        return false;
    }
    path->segment[path->count] = field->field;
    path->is_member[path->count] = node->node_type == AST_NODE_MEMBER_EXPR;
    path->count++;
    return true;
}

// The shape half of "does this initializer hand back a freshly-owned
// container": literals, calls and `new` produce a new owner, while member and
// index reads are borrows of an existing one. The IDENT case is deliberately
// not here -- it propagates the *source binding's* own answer, which each tier
// resolves through its own binding table (MirVarEntry / NameEntry::cow_owned).
static inline bool ast_expr_produces_owned_container(AstNode* root_expr) {
    if (!root_expr) return false;
    switch (root_expr->node_type) {
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT:
    case AST_NODE_LIST:
    case AST_NODE_OBJECT_LITERAL:
    case AST_NODE_NEW_EXPR:
    case AST_NODE_CALL_EXPR:
        return true;
    default:
        return false;
    }
}

// Whether a binding's initializer may hand back a container the writer would
// share with another root — the condition that makes an alias an ownership
// boundary (cow_bind_var).
static inline bool ast_expr_may_return_container(AstNode* expr, TypeId expr_tid,
        TypeId target_tid) {
    if (ast_type_needs_mutable_clone(expr_tid) && expr_tid != LMD_TYPE_ANY) return true;
    if (expr_tid != LMD_TYPE_ANY && target_tid != LMD_TYPE_ANY) return false;

    AstNode* root_expr = ast_unwrap_primary(expr);
    if (!root_expr) return target_tid == LMD_TYPE_ANY;
    switch (root_expr->node_type) {
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT:
    case AST_NODE_LIST:
    case AST_NODE_IDENT:
    case AST_NODE_INDEX_EXPR:
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_CALL_EXPR:
    case AST_NODE_IF_EXPR:
    case AST_NODE_MATCH_EXPR:
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
        return true;
    default:
        // `any` arithmetic/comparison paths are scalar in practice; cloning
        // their boxed result turns tight integer loops into runtime calls.
        return false;
    }
}

typedef struct AstBlockNode : AstNode {
    AstNode* statements;
    NameScope* vars;
} AstBlockNode;

typedef struct AstExprStmtNode : AstNode {
    AstNode* expression;
} AstExprStmtNode;

typedef struct AstVarDeclNode : AstNode {
    AstNode* declarations;
    int kind;
    bool is_using;
    bool is_await_using;
} AstVarDeclNode;

typedef struct AstDeclaratorNode : AstNode {
    AstNode* id;
    AstNode* init;
    TsTypeAnnotationNode* ts_type;
} AstDeclaratorNode;

typedef struct AstSpreadNode : AstNode {
    AstNode* argument;
} AstSpreadNode;

typedef struct AstForOfNode : AstNode {
    AstNode* left;
    AstNode* init;
    AstNode* right;
    AstNode* body;
    int kind;
    bool declares_binding;
    bool is_await;
} AstForOfNode;

typedef struct AstTryNode : AstNode {
    AstNode* block;
    AstNode* handler;
    AstNode* finalizer;
} AstTryNode;

typedef struct AstCatchNode : AstNode {
    AstNode* param;
    AstNode* body;
} AstCatchNode;

// Forward declare for capture list
struct FnCapture;
struct FnAnalysis;

// aligned with AstNamedNode on name
typedef struct AstFuncNode : AstNode {
    String* name;
    union {
        AstNamedNode *param;
        AstNode *params;
    };
    AstNode *body;
    NameScope *vars;
    struct FnCapture* captures;
    struct FnAnalysis* analysis;
    bool is_arrow;
    bool is_async;
    bool is_generator;
    bool has_use_strict_directive;
    TsTypeAnnotationNode* ts_return_type;
} AstFuncNode;

typedef struct AstMethodNode : AstFuncNode {
    AstNode* key;
    enum {
        JS_METHOD_METHOD,
        JS_METHOD_CONSTRUCTOR,
        JS_METHOD_GET,
        JS_METHOD_SET
    } kind;
    bool computed;
    bool static_method;
} AstMethodNode;

typedef struct FnCapture {
    const char* name;
    const char* scope_env_key;
    String* lambda_name;
    NameEntry* entry;
    int scope_env_slot;
    int private_env_slot;
    int grandparent_slot;
    int parent_env_link_slot_override;
    bool is_mutable;
    bool is_let_const;
    bool is_const;
    bool is_nfe_binding;
    bool force_env_capture;
    struct FnCapture* next;
} FnCapture;

// Why an expression's static type fell back to `any`. Every ANY assignment in
// the Lambda and JS builders names its reason so "where does ANY come from" is
// a report instead of an archaeology pass [Type_Infer TI3]. The catalog IDs in
// the comments are the TIG# gap entries the reason tracks.
typedef enum AnyReason {
    ANY_OPEN_PARAM = 0,      // untyped parameter — genuinely open
    ANY_OPEN_MAP,            // unshaped/open map or element
    ANY_DYNAMIC_NAME,        // computed member/field name
    ANY_EXPLICIT,            // source wrote `any`
    ANY_SYSFUNC_ROW,         // registry row has no precise success type (TIG4)
    ANY_INDEX_ELEM,          // a[i] with unknown element type (TIG1)
    ANY_MEMBER_SHAPE,        // field not resolvable from the shape (TIG2/TIG3)
    ANY_JOIN,                // if/match arms disagree (TIG7/TIG8)
    ANY_LOGICAL_AND,         // `and` result (TIG5)
    ANY_COMPARE,             // relational operands not proven comparable (TIG6)
    ANY_LIST,                // list/content literal (TIG11)
    ANY_UNARY,               // unary +/- on non-numeric (TIG12)
    ANY_LOOP_SRC,            // for-loop source element type unknown (TIG10)
    ANY_DECOMPOSE,           // destructuring target (TIG15)
    ANY_PIPE,                // pipe result element type (TIG16)
    ANY_JS_BINARY,           // JS binary expression (TIG13)
    ANY_JS_CALL_MEMBER,      // JS call/member result (TIG14)
    ANY_JS_CALL,             // JS call result specifically (TIG14a)
    ANY_JS_MEMBER,           // JS member/subscript read specifically (TIG14b)
    ANY_ARITH_OPERAND,       // arithmetic where an operand is not statically numeric
    ANY_JOIN_OP,             // `++` join/concat result
    ANY_CALL_RESULT,         // callee's return type unknown (recursive/open fn)
    ANY_WIDENED_VAR,         // mutable binding widened by reassignment
    ANY_STATEMENT,           // statement node carries no value type
    ANY_ERROR_RECOVERY,      // a diagnostic already fired; ANY avoids cascades
    ANY_LEGACY_UNCLASSIFIED, // not yet classified — must trend to zero
    ANY_REASON_COUNT
} AnyReason;

// C linkage: this header is reached both directly and through ast.hpp's
// `extern "C"` block, so an unqualified declaration would mangle differently
// per includer and fail to link.
#ifdef __cplusplus
extern "C"
#endif
const char* any_reason_name(AnyReason reason);

typedef struct FnParamEvidence {
    int evidence;
    int int_evidence;
    int float_evidence;
    int string_evidence;
    int other_evidence;
    int name_count;
    // String* entries contain the parameter binding followed by any aliases.
    // The list is compiler-owned scratch state, so inference has no fixed
    // alias count or source-name width.
    ArrayList* names;
    bool used_as_container;
    TypeId container_store_type;
    bool container_store_conflict;
    bool compared_with_non_numeric;
    bool param_reassigned;
} FnParamEvidence;

// root of the AST
typedef struct AstScript : AstNode {
    union {
        AstNode *child;
        AstNode *body;
    };
    NameScope *global_vars;
    bool has_use_strict_directive;
} AstScript;

typedef struct AstClassNode : AstNode {
    String* name;
    AstNode* superclass;
    AstNode* body;
} AstClassNode;

typedef struct AstClassFieldNode : AstNode {
    AstNode* key;
    AstNode* value;
    bool is_static;
    bool is_private;
    bool computed;
} AstClassFieldNode;

// Object (no content): type Point { x: float, y: float; fn magnitude() => ... }
// Element (with content): type Article { title: string\n string, element }
typedef struct AstObjectTypeNode : AstNamedNode {
    AstNode* item;
    AstNode* base_type;
    AstNode* content;
    AstNode* methods;
    AstNode* constraints;
    bool is_public;
    int local_type_index;
} AstObjectTypeNode;

// Object literal node: {TypeName key: value, ...}
typedef struct AstObjectLiteralNode : AstMapNode {
    String* type_name;
} AstObjectLiteralNode;

typedef struct AstYieldNode : AstNode {
    AstNode* argument;
    bool delegate;
} AstYieldNode;

typedef struct AstAwaitNode : AstNode {
    AstNode* argument;
} AstAwaitNode;

typedef struct AstImportNode : AstNode {
    String* source;
    AstNode* specifiers;
    String* default_name;
    String* namespace_name;
    String* alias;
    StrView module;
    Script* script;
    bool is_relative;
    bool is_cross_lang;
} AstImportNode;

typedef AstImportNode AstImportDeclNode;

typedef struct AstImportSpecifierNode : AstNode {
    String* local_name;
    String* remote_name;
} AstImportSpecifierNode;

typedef struct AstExportDeclNode : AstNode {
    AstNode* declaration;
    AstNode* specifiers;
    String* source;
    bool is_default;
    bool is_star;
    bool is_namespace;
} AstExportDeclNode;

typedef struct AstExportSpecifierNode : AstNode {
    String* local_name;
    String* export_name;
} AstExportSpecifierNode;

typedef struct FnEffectSummary {
    bool may_gc;
    bool may_reenter;
    bool may_set_exception;
    bool may_return_error;
    bool may_suspend;
    bool has_unknown_call;
} FnEffectSummary;
typedef struct FnEntryAnalysis {
    FnEntryKind kind;
    bool can_use_bound_context;
    bool has_dynamic_scope;
    bool requires_arguments_object;
    bool is_external_entry;
} FnEntryAnalysis;
typedef struct FnReturnLaneAnalysis {
    TypeId semantic_type;
    ValueRep abi_rep;
    ScalarReturnClass scalar_class;
} FnReturnLaneAnalysis;
typedef struct FnReturnAnalysis {
    FnReturnLaneAnalysis normal;
    FnReturnLaneAnalysis error;
    FnErrorLane error_lane;
    // v3 (RV1/RV10): the single source of truth for how this entry returns. No
    // emitter site may recompute it locally — that divergence is the v27
    // havlak wrong-answer bug class.
    FnReturnShape shape;
    // Where lane 2 travels for this entry (RV10a/RV12). Independent of shape.
    FnCompanionTransport companion;
} FnReturnAnalysis;
typedef struct FnParamTypeInfo {
    TypeId semantic_type;
    TypeId inferred_elem_type;
    uint32_t flags;
} FnParamTypeInfo;
enum {
    FN_PARAM_FLAG_DECLARED = 1u << 0,
    FN_PARAM_FLAG_INFERRED_SPECIALIZATION = 1u << 1,
};
typedef struct FnParamAnalysis {
    TypeId semantic_type;
    ValueRep canonical_rep;
    uint32_t demand_mask;
} FnParamAnalysis;
typedef struct FnBindingAnalysis {
    NameEntry* name;
    TypeId semantic_type;
    ValueRep canonical_rep;
    JitValueClass value_class;
    BindingStorage storage;
    uint32_t escape_flags;
} FnBindingAnalysis;
typedef struct FnValueAnalysis {
    AstNode* producer;
    TypeId semantic_type;
    ValueRep actual_rep;
    uint32_t demand_mask;
    bool is_exact_constant;
    uint64_t constant_bits;
} FnValueAnalysis;
typedef struct FnVariantAnalysis {
    FnEntryAnalysis entry;
    FnEffectSummary effects;
    FnReturnAnalysis result;
    FnParamAnalysis* params;
    int param_count;
    FnBindingAnalysis* bindings;
    int binding_count;
    FnValueAnalysis* values;
    int value_count;
} FnVariantAnalysis;

// P2 tier-up state belongs to the definition site, rather than to individual
// Function values: aliases of one `fn` must observe the same immutable native
// entry once it is published (D8.1.1v2 §5.1).
typedef enum FnPromotionState {
    FN_PROMOTION_INTERP,
    FN_PROMOTION_COMPILING,
    FN_PROMOTION_COMPILED,
    FN_PROMOTION_PINNED_INTERP,
} FnPromotionState;

typedef struct FnPromotionCell {
    FnPromotionState state;
    uint32_t call_count;
    uint32_t backedge_count;
    // self-tail edges are eligible for entry-equivalent handoff, unlike a
    // general loop backedge which has no native frame materialization point
    // (D8.1.1v5).
    uint32_t tail_edge_count;
    void* boxed_entry;
} FnPromotionCell;

typedef struct FnAnalysis {
    FnCapture* captures;
    FnParamEvidence* evidence;
    FnParamTypeInfo* param_types;
    int param_count;
    int capture_count;
    int evidence_count;
    bool may_await;
    bool needs_task_context;
    bool has_indirect_pn_call;
    int await_point_count;
    int async_fault_handler_count;
    const char* may_await_cause;
    FnVariantAnalysis variants[4];
    int variant_count;
    // T0 activation shape, filled by the frame-plan pass (AI5). FnAnalysis is
    // the designated carrier for per-function facts, so the plan lives here
    // rather than on AstFuncNode.
    FnFramePlan frame_plan;
    // The binding a named `fn`/`pn` declaration installs. AstFuncNode cannot
    // carry it: push_name deliberately writes no AstNamedNode-only field
    // because that alias is `vars` on a function and the join pointer on a loop.
    NameEntry* decl_entry;
    // T0/T1 transition state. The Script owns the AST and all generated
    // satellites, so this cell has the same lifetime as its definition.
    FnPromotionCell promotion;
} FnAnalysis;

static inline FnVariantAnalysis* fn_analysis_variant(
        FnAnalysis* analysis, FnEntryKind kind) {
    if (!analysis) return NULL;
    for (int i = 0; i < analysis->variant_count; i++) {
        if (analysis->variants[i].entry.kind == kind) {
            return &analysis->variants[i];
        }
    }
    return NULL;
}

typedef union FnExt {
    void* lambda;
    void* js;
    void* ptr;
} FnExt;

typedef struct ClauseNodeBase : AstNode {
    AstNode* body;
} ClauseNodeBase;

typedef struct LangProfile {
    const char* name;
    void (*validate)(void* ctx, AstNode* root);
    void (*analyze)(void* ctx, AstNode* root);
    void (*lower)(void* ctx, AstNode* root);
    void (*visit_ext_children)(AstNode* node, AstChildVisitor visitor, void* ctx);
} LangProfile;

static inline void lang_profile_noop_hook(void* ctx, AstNode* root) {
    (void)ctx;
    (void)root;
}

static inline void lang_profile_noop_ext(AstNode* node, AstChildVisitor visitor, void* ctx) {
    (void)node;
    (void)visitor;
    (void)ctx;
}

inline LangProfile lambda_profile = {
    "lambda",
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_ext,
};

inline LangProfile js_profile = {
    "js",
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_ext,
};

static inline LangProfile* lang_profile_for_name(const char* name) {
    if (!name) return &lambda_profile;
    if ((name[0] == 'j' || name[0] == 'J') && (name[1] == 's' || name[1] == 'S') && name[2] == '\0') {
        return &js_profile;
    }
    // Hosted languages carry their semantic hooks inside their module; the
    // shared profile table must not grow a dormant branch per guest language.
    return &lambda_profile;
}
