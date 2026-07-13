#pragma once

#include <stdint.h>
#include <tree_sitter/api.h>
#include "lambda.h"

typedef struct NamePool NamePool;
typedef struct Script Script;
typedef struct AstNode AstNode;
typedef struct AstImportNode AstImportNode;
typedef struct NameEntry NameEntry;
typedef struct NameScope NameScope;

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

    // L4 callable forms
    AST_NODE_FUNC = 300,
    AST_NODE_FUNC_EXPR = 301,
    AST_NODE_PROC = 302,

    // L5/L6 type/module/class forms that are not yet structurally unified.
    AST_NODE_CLASS = 350,
    AST_NODE_FIELD = 351,

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
    AST_NODE_EVENT_HANDLER = 541,
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
    OPERATOR_JS_ASSIGN,
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

typedef struct FnAnalysis {
    void* captures;
    void* evidence;
} FnAnalysis;

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
    return &lambda_profile;
}
