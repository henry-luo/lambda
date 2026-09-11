#pragma once

#include "../runtime/ast.hpp"
#include "../lambda-data.hpp"

// JavaScript AST node types share AstNodeType's underlying space. Core-shaped
// nodes use core values now; JS-only or not-yet-merged variants stay in 1000–1499.
typedef AstNodeType JsAstNodeType;

static const JsAstNodeType JS_AST_NODE_TEMPLATE_LITERAL = (JsAstNodeType)1002;
static const JsAstNodeType JS_AST_NODE_TEMPLATE_ELEMENT = (JsAstNodeType)1003;
static const JsAstNodeType JS_AST_NODE_STATIC_BLOCK = (JsAstNodeType)1006;
static const JsAstNodeType JS_AST_NODE_FINALLY_CLAUSE = (JsAstNodeType)1009;
static const JsAstNodeType JS_AST_NODE_LABELED_STATEMENT = (JsAstNodeType)1022;
static const JsAstNodeType JS_AST_NODE_REGEX = (JsAstNodeType)1023;
static const JsAstNodeType JS_AST_NODE_WITH_STATEMENT = (JsAstNodeType)1026;
static const JsAstNodeType JS_AST_NODE_TAGGED_TEMPLATE = (JsAstNodeType)1027;

typedef Operator JsOperator;


typedef AstNode JsAstNode;
typedef AstIdentNode JsIdentifierNode;
typedef AstLiteralNode JsLiteralNode;
typedef AstBinaryNode JsBinaryNode;
typedef AstUnaryNode JsUnaryNode;
typedef AstAssignNode JsAssignmentNode;
typedef AstFuncNode JsFunctionNode;
typedef AstCallNode JsCallNode;
typedef AstFieldNode JsMemberNode;
typedef AstArrayNode JsArrayNode;
typedef AstMapNode JsObjectNode;

typedef AstPropertyNode JsPropertyNode;

typedef AstVarDeclNode JsVariableDeclarationNode;
typedef AstDeclaratorNode JsVariableDeclaratorNode;
typedef struct JsIfNode : AstIfNode {
    // Unbraced branches still own a lexical scope for Annex B block-function
    // bindings. Keep those scopes on the JS extension so the AST executor can
    // materialize the same environment that binding selected.
    NameScope* consequent_vars;
    NameScope* alternate_vars;
} JsIfNode;
typedef AstWhileNode JsWhileNode;

typedef AstForStmtNode JsForNode;

typedef AstReturnNode JsReturnNode;
typedef AstBlockNode JsBlockNode;
typedef AstExprStmtNode JsExpressionStatementNode;
typedef AstScript JsProgramNode;

typedef AstIfNode JsConditionalNode;

// JavaScript template literal node
typedef struct JsTemplateLiteralNode : JsAstNode {
    JsAstNode* quasis;              // Template elements (strings)
    JsAstNode* expressions;         // Interpolated expressions
} JsTemplateLiteralNode;

// JavaScript template element node
typedef struct JsTemplateElementNode : JsAstNode {
    String* raw;                    // Raw string value
    String* cooked;                 // Processed string value
    bool tail;                      // Is this the last element
} JsTemplateElementNode;

// JavaScript tagged template expression node: tag`...`
typedef struct JsTaggedTemplateNode : JsAstNode {
    JsAstNode* tag;                 // Tag function expression
    JsTemplateLiteralNode* quasi;   // Template literal
} JsTaggedTemplateNode;

typedef AstSpreadNode JsSpreadElementNode;
typedef struct JsClassNode : AstClassNode {
    // A named class expression owns a private lexical self binding. Class
    // declarations use their surrounding lexical declaration instead.
    NameScope* expression_scope;
} JsClassNode;

typedef AstMethodNode JsMethodDefinitionNode;

typedef AstClassFieldNode JsFieldDefinitionNode;

// JavaScript class static block node: static { ... }
typedef struct JsStaticBlockNode : JsAstNode {
    JsAstNode* body;                // Block statement body
} JsStaticBlockNode;

typedef AstTryNode JsTryNode;
typedef struct JsCatchNode : AstCatchNode {
    // Catch parameters live in a handler environment outside the body block.
    NameScope* vars;
} JsCatchNode;

typedef AstRaiseNode JsThrowNode;

typedef AstArrayNode JsArrayPatternNode;
typedef AstMapNode JsObjectPatternNode;
typedef AstAssignNode JsAssignmentPatternNode;
typedef struct JsSwitchNode : AstMatchNode {
    // All cases share one lexical environment. The core prefix retains the
    // discriminant/case edges; the JS executor retains the binding carrier.
    NameScope* vars;
} JsSwitchNode;
typedef AstMatchArm JsSwitchCaseNode;
typedef AstDoWhileNode JsDoWhileNode;
typedef struct JsForOfNode : AstForOfNode {
    // The loop head owns a lexical environment, including the fresh
    // per-iteration binding cells required by let/const closures.
    NameScope* vars;
} JsForOfNode;

// Reuse same struct for for...in
typedef JsForOfNode JsForInNode;

typedef AstArrayNode JsSequenceNode;

typedef AstBreakContinueNode JsBreakContinueNode;

// v11: Labeled statement node
typedef struct JsLabeledStatementNode : JsAstNode {
    const char* label;               // Label name
    int label_len;                   // Length of label name
    JsAstNode* body;                 // Labeled statement body
} JsLabeledStatementNode;

// v17: With statement node
typedef struct JsWithStatementNode : JsAstNode {
    JsAstNode* object;               // Expression in with(expr)
    JsAstNode* body;                 // Body statement
} JsWithStatementNode;

// v11: Regex literal node
typedef struct JsRegexNode : JsAstNode {
    const char* pattern;             // Regex pattern (without slashes)
    int pattern_len;
    const char* flags;               // Regex flags (g, i, m, etc.)
    int flags_len;
} JsRegexNode;

typedef AstYieldNode JsYieldNode;
typedef AstAwaitNode JsAwaitNode;
typedef AstImportDeclNode JsImportNode;

typedef AstImportSpecifierNode JsImportSpecifierNode;

// ---------------------------------------------------------------------------
// Generic child traversal
//
// js_ast_children.cpp holds one description of every node kind's child edges,
// in source order. A walker keeps only the cases it cares about and delegates
// the rest here; a walker that deliberately skips a child must say so as an
// explicit case rather than relying on the table.
// ---------------------------------------------------------------------------
typedef void (*JsAstChildVisit)(JsAstNode* child, void* ctx);
typedef bool (*JsAstChildPredicate)(JsAstNode* child, void* ctx);

// Parameter shape is source-owned: interpreter metadata and MIR planning must
// not independently rediscover default/rest and binding-name semantics.
struct JsAstParameterFacts {
    int formal_length = -1;
    bool has_default_params = false;
    bool has_duplicate_param_names = false;
    JsIdentifierNode* first_duplicate_param = NULL;
    bool has_rest_param = false;
    bool has_non_simple_params = false;
};

// Visit every child in source order; list-valued edges walk their ->next chain.
void js_ast_visit_children(JsAstNode* node, JsAstChildVisit visit, void* ctx);
// Short-circuiting variant: stops at the first child the predicate accepts.
bool js_ast_any_child(JsAstNode* node, JsAstChildPredicate predicate, void* ctx);
// Visit only destructuring binding targets. Keys and default-value expressions
// are semantic reads, so they deliberately remain outside this edge set.
void js_ast_visit_binding_pattern_children(JsAstNode* node,
    JsAstChildVisit visit, void* ctx);
bool js_ast_any_binding_pattern_child(JsAstNode* node,
    JsAstChildPredicate predicate, void* ctx);
JsIdentifierNode* js_ast_parameter_binding_identifier(JsAstNode* parameter);
JsAstParameterFacts js_ast_collect_parameter_facts(JsAstNode* parameters);
// Structural gate: every JS-only child layout must appear in the extension table.
bool js_ast_child_catalog_complete(void);

enum JsAstObservation {
    JS_AST_OBSERVES_ARGUMENTS = 1u << 0,
    JS_AST_OBSERVES_THIS = 1u << 1,
    JS_AST_OBSERVES_NEW_TARGET = 1u << 2,
};
struct JsAstFunctionFacts {
    uint8_t observations = 0;
    bool has_direct_eval = false;
    bool has_with = false;
    bool has_direct_super_call = false;
    uint32_t first_direct_super_call_start = 0;
    // A super() written inside a nested arrow. It binds `this` lexically for
    // this constructor, but at a program point no source-order scan can place.
    bool has_lexical_super_call = false;
    bool tail_reuse_safe = false;
};
// Facts intentionally stop at each semantic function boundary, except that
// arrows contribute their lexical observations to the enclosing function.
JsAstFunctionFacts js_ast_collect_function_facts(JsAstNode* params,
                                                  JsAstNode* body);
bool js_ast_is_proto_literal_key(JsAstNode* key);

// Adapter for the shared AstIndex walker. Core-shaped JavaScript nodes are
// already traversed by ast_visit_core_children(); this reports only the JS
// extension range so an index has one complete child contract without visits
// being duplicated.
void js_ast_visit_extension_children(AstNode* node, AstChildVisitor visitor,
                                     void* ctx);
bool js_ast_publish_extension_facts(AstNode* node, struct AstIndex* index);

typedef AstExportDeclNode JsExportNode;

typedef AstExportSpecifierNode JsExportSpecifierNode;

static inline bool js_ast_identifier_named(JsAstNode* node, const char* name,
        size_t length) {
    if (!node || node->node_type != AST_NODE_IDENT) return false;
    String* value = ((JsIdentifierNode*)node)->name;
    return value && value->len == length && strncmp(value->chars, name, length) == 0;
}
