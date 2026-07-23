#pragma once

#include <stdint.h>
#include <tree_sitter/api.h>
#include "../lambda.h"
#include "value_rep.h"

typedef struct NamePool NamePool;
typedef struct Script Script;
typedef struct AstNode AstNode;
typedef struct AstImportNode AstImportNode;
typedef struct NameEntry NameEntry;
typedef struct NameScope NameScope;
typedef struct TsTypeAnnotationNode TsTypeAnnotationNode;

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
    AST_NODE_PARENT_EXPR = 518,
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
} AstNodeType;

typedef enum Operator {
    // unary
    OPERATOR_NOT,
    OPERATOR_NEG,
    OPERATOR_POS,
    OPERATOR_SPREAD,
    OPERATOR_IS_ERROR,

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
} Operator;

typedef enum AstLiteralType {
    AST_LITERAL_NUMBER,
    AST_LITERAL_STRING,
    AST_LITERAL_BOOLEAN,
    AST_LITERAL_NULL,
    AST_LITERAL_UNDEFINED,
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
    bool is_mutable;
    bool is_var_param;
    bool has_type_annotation;
    bool type_widened;
    bool is_lexical;
    bool is_const;
    bool tdz_active;
    bool is_exported;
};

// name_scope
struct NameScope {
    NameEntry* first;
    NameEntry* last;
    bool is_proc;
    NameScope* parent;
    ScopeKind kind;
    bool strict;
};

struct AstNode {
    AstNodeType node_type;
    Type *type;
    AstNode* next;
    TSNode node;
};

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
} AstCallNode;

typedef struct AstStartNode : AstNode {
    AstCallNode* call;
    NameScope* owner_scope;
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
    String* error_name;
    NameEntry* entry;
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
    int lexical_for_head_capture_count;
    char lexical_for_head_capture_names[8][64];
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
    char name[128];
    char scope_env_key[128];
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

enum {
    FN_PARAM_MAX_ALIASES = 8,
};

typedef struct FnParamEvidence {
    int evidence;
    int int_evidence;
    int float_evidence;
    int string_evidence;
    int other_evidence;
    int name_count;
    char names[FN_PARAM_MAX_ALIASES][64];
    int name_lens[FN_PARAM_MAX_ALIASES];
    bool used_as_container;
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
    bool may_need_caller_scalar_home;
} FnReturnLaneAnalysis;
typedef struct FnReturnAnalysis {
    FnReturnLaneAnalysis normal;
    FnReturnLaneAnalysis error;
    FnErrorLane error_lane;
    uint8_t scalar_home_lane_mask;
} FnReturnAnalysis;
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
typedef struct FnAnalysis {
    FnCapture* captures;
    FnParamEvidence* evidence;
    int capture_count;
    int evidence_count;
    bool may_await;
    bool needs_task_context;
    bool has_indirect_pn_call;
    int await_point_count;
    const char* may_await_cause;
    FnVariantAnalysis variants[4];
    int variant_count;
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
} LangProfile;

static inline void lang_profile_noop_hook(void* ctx, AstNode* root) {
    (void)ctx;
    (void)root;
}

inline LangProfile lambda_profile = {
    "lambda",
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_hook,
};

inline LangProfile js_profile = {
    "js",
    lang_profile_noop_hook,
    lang_profile_noop_hook,
    lang_profile_noop_hook,
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
