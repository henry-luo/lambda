#pragma once

// Lambda AST construction seams.
//
// The first-party recursive-descent parser drives a syntax sink that builds
// the retained nodes as productions complete; a separate resolve pass then
// binds names, assigns types, registers constants and types, and reports
// diagnostics (D8.2.5v3). Both halves live in build_ast.cpp so the parser and
// the semantic layer never grow independent inference or diagnostics.

#include "ast.hpp"
#include "parser/lambda_rd_parser.h"

// Span allocation is the only AST allocation primitive the direct parser may
// use.
AstNode* alloc_ast_node_from_span(Transpiler* tp, AstNodeType node_type,
        SourceSpan span, size_t size);

// How the syntax phase built a node, stored in AstNode::syntax_form so the
// resolve pass can perform the semantic construction the parser defers:
// name binding, typing, constant/type registration and diagnostics, in the
// order the productions completed. LSF_NONE marks a resolved node, or one
// whose syntax already fixes everything (a `null` literal, a path root).
typedef enum LambdaSyntaxForm : uint8_t {
    LSF_NONE = 0,
    // `T as name`: an AST_NODE_TYPE allocated as AstNamedNode so it can carry
    // its base pattern (`as`) and binder `name` until resolution.
    LSF_BINDER,
    // ---- type-pattern sub-language (parse_type_pattern.cpp) ----
    LSF_TP_FIRST,
    LSF_TP_LIT_STRING = LSF_TP_FIRST, // PRIMARY; flag symbol; literal_value = String*
    LSF_TP_LIT_NUMBER,        // PRIMARY; flag float; literal_value = value bits
    LSF_TP_LIT_BOOL,          // PRIMARY
    LSF_TP_ANY,               // PRIMARY: the `any` operand of a type `!T`
    LSF_TP_BASE,              // AST_NODE_TYPE; aux = base-type table index
    LSF_TP_NAME,              // IDENT; a binder reference morphs to AST_NODE_TYPE
    LSF_TP_CHAR_CLASS,        // PATTERN_CHAR_CLASS
    LSF_TP_PATTERN_REF,       // IDENT naming a pattern inside an island
    LSF_TP_PATTERN_RANGE,     // PATTERN_RANGE of two string literals
    LSF_TP_ISLAND_GROUP,      // LIST_TYPE: a parenthesized island body
    LSF_TP_ISLAND_UNARY,      // UNARY_TYPE: island negation or occurrence
    LSF_TP_ISLAND_SEQ,        // PATTERN_SEQ
    LSF_TP_ISLAND,            // PATTERN_ISLAND
    LSF_TP_BINARY,            // BINARY_TYPE over wrapped operand types
    LSF_TP_REGISTERED_BINARY, // BINARY_TYPE over unwrapped operand types
    LSF_TP_RANGE,             // BINARY `lo to hi`
    LSF_TP_OCCURRENCE,        // UNARY_TYPE occurrence or array suffix
    LSF_TP_OPTIONAL_FIELD,    // UNARY_TYPE for a `name?:` field
    LSF_TP_ARRAY,             // ARRAY_TYPE
    LSF_TP_FIELD,             // KEY_EXPR: map field or element attribute
    LSF_TP_MAP,               // MAP_TYPE
    LSF_TP_TUPLE,             // LIST_TYPE
    LSF_TP_CONTENT,           // CONTENT_TYPE of an element type
    LSF_TP_ELEMENT,           // ELMT_TYPE
    LSF_TP_FN_PARAM,          // KEY_EXPR: one fn-type parameter
    LSF_TP_FN,                // FUNC_TYPE
    LSF_TP_RETURN_CONTRACT,   // FUNC_TYPE wrapper of a declaration return contract
    LSF_TP_LAST = LSF_TP_RETURN_CONTRACT,
    // ---- main grammar (build_ast.cpp) ----
    LSF_LITERAL,              // PRIMARY; aux = LambdaAstLiteralKind
    LSF_BASE_TYPE,            // TYPE (ident-sized): may resolve to a system function
    LSF_IDENT,                // IDENT: a name read (morphs by what it resolves to)
    LSF_MEMBER_NAME,          // IDENT: a member key spelling, never looked up
    LSF_WRAPPER,              // PRIMARY around one expression
    LSF_CURRENT_ITEM,         // CURRENT_ITEM / CURRENT_INDEX
    LSF_CURRENT_ERROR,        // CURRENT_ERROR
    LSF_NAVIGATION,           // NAVIGATION_EXPR
    LSF_PATH_INDEX,           // PATH_INDEX_EXPR
    LSF_SPREAD,               // SPREAD
    LSF_RAISE,                // RAISE_EXPR
    LSF_TYPE_NEGATION,        // BINARY_TYPE of a value-position `!T`
    LSF_ADDRESS_OF,           // UNARY `&`
    LSF_UNARY,                // UNARY `not` / `-` / `+`
    LSF_FORCE,                // UNARY `#`
    LSF_PROPAGATE,            // UNARY `^`; becomes its call operand when it has one
    LSF_GROUP_WRAPPER,        // PRIMARY of a one-item parenthesized group
    LSF_GROUP_LIST,           // LIST of any other parenthesized group
    LSF_ARRAY,                // ARRAY
    LSF_MAP,                  // MAP
    LSF_ELEMENT,              // ELEMENT; may resolve to an OBJECT_LITERAL
    LSF_DECOMPOSE,            // DECOMPOSE
    LSF_DECLARATOR,           // VARIABLE_DECLARATOR of a `let`/`var`/`pub` binding
    LSF_FUNCTION,             // FUNC / FUNC_EXPR / PROC
    LSF_PARAM,                // PARAM
    LSF_IF,                   // IF_EXPR
    LSF_MATCH,                // MATCH_EXPR
    LSF_MATCH_ARM,            // MATCH_ARM
    LSF_FOR,                  // FOR_EXPR
    LSF_WHILE,                // LOOP
    LSF_FOR_BINDING,          // FOR_CLAUSE
    LSF_FOR_LET,              // VARIABLE_DECLARATOR of a for-header `let`
    LSF_ORDER_SPEC,           // ORDER_SPEC
    LSF_GROUP_KEY,            // GROUP_KEY
    LSF_GROUP_CLAUSE,         // GROUP_CLAUSE
    LSF_BINARY,               // BINARY (PIPE for `|>` / `that`)
    LSF_MEMBER,               // MEMBER_EXPR; may resolve to a constant or import
    LSF_INDEX,                // INDEX_EXPR
    LSF_QUERY,                // QUERY_EXPR
    LSF_HANDLER,              // HANDLER_EXPR / HANDLER_STAM
    LSF_CALL,                 // CALL_EXPR; may resolve to START
    LSF_CONSTRAINED,          // CONSTRAINED_TYPE
    LSF_IMPORT,               // IMPORT; may resolve to a NULL marker
    LSF_TYPE_STAM,            // TYPE_STAM / PUB_STAM of a type alias
    LSF_PATTERN_DEF,          // STRING_PATTERN / SYMBOL_PATTERN
    LSF_TYPE_ALIAS,           // VARIABLE_DECLARATOR of `type Name = T`
    LSF_OBJECT_TYPE,          // OBJECT_TYPE
    LSF_OBJECT_FIELD,         // KEY_EXPR field of an object type
    LSF_OBJECT_CONTENT,       // CONTENT_TYPE of an object type
    LSF_PUB_STAM,             // PUB_STAM of a `pub` binding
    LSF_ASSIGN,               // assignment; kind chosen when resolved
    LSF_RETURN,               // RETURN_STAM
    LSF_BREAK,                // BREAK_STAM / CONTINUE_STAM
    LSF_CRUD,                 // CRUD_STAM `put` / `del`; aux = write op
    LSF_CRUD_MARK,            // CRUD_STAM sequence / `commit` / `rollback`
    LSF_OPEN,                 // OPEN_STAM
    LSF_VAR_STAM,             // VAR_STAM
    LSF_LET_STAM,             // LET_STAM
    LSF_NAMED_ARG,            // NAMED_ARG
    LSF_KEY_ITEM,             // KEY_EXPR of a map item or element attribute
    LSF_CONTENT,              // CONTENT
    LSF_VIEW,                 // VIEW
    LSF_VIEW_STATE,           // STATE_ENTRY
    LSF_EVENT_HANDLER,        // EVENT_HANDLER
    LSF_SCRIPT,               // AST_SCRIPT
} LambdaSyntaxForm;

static inline bool lambda_syntax_form_is_type_pattern(uint8_t form) {
    return form >= LSF_TP_FIRST && form <= LSF_TP_LAST;
}

// Base-type spellings are lexical, so the syntax phase classifies them with no
// side effects; resolution then yields the Type (and counts an explicit
// `any`). A negative index means the word is not a base type.
int lambda_base_type_index(StrView name);
Type* lambda_base_type_from_index(Transpiler* tp, int index);

// `T as name` binder sites. The syntax half only records base and name; the
// resolve half performs the collision checks and registers the binder in the
// current scope, morphing the node into the error type on rejection.
AstNode* build_binder_type_syntax(Transpiler* tp, SourceSpan span,
        AstNode* base, StrView name);
void resolve_binder_type(Transpiler* tp, AstNode* node);

// Move a direct-parser fragment's byte ranges into an append-only REPL source
// buffer. The parser receives only the new fragment, so its local offsets must
// be rebased before the fragment is attached to the session AST.
void lambda_ast_shift_source_spans(AstNode* root, uint32_t byte_offset);

// Shared Lambda traversal, including procedural statements missing from the
// language-neutral core visitor. False prunes the current subtree.
typedef bool (*LambdaAstVisitor)(AstNode* node, void* data);
void walk_lambda_ast(AstNode* node, LambdaAstVisitor visitor, void* data,
        bool descend_functions);

// Lexer-neutral literal categories. The C lexer maps directly to these
// categories.
typedef enum LambdaAstLiteralKind {
    LAMBDA_AST_LITERAL_STRING,
    LAMBDA_AST_LITERAL_SYMBOL,
    LAMBDA_AST_LITERAL_BINARY,
    LAMBDA_AST_LITERAL_DATETIME,
    LAMBDA_AST_LITERAL_NAMED_VALUE,
    LAMBDA_AST_LITERAL_INTEGER,
    LAMBDA_AST_LITERAL_FLOAT,
    LAMBDA_AST_LITERAL_DECIMAL,
    LAMBDA_AST_LITERAL_SIZED_INTEGER,
    LAMBDA_AST_LITERAL_SIZED_FLOAT,
    LAMBDA_AST_LITERAL_IMAGINARY,
} LambdaAstLiteralKind;

// Maps an already-committed Lambda operator spelling to the retained AST
// operator. Prefix and infix spellings intentionally use separate entry
// points: `+`, `-`, `*`, and `!` have different retained meanings by form.
bool lambda_unary_operator_from_spelling(StrView spelling, Operator* op_out);
bool lambda_binary_operator_from_spelling(StrView spelling, Operator* op_out);

// Builds and binds a name read at resolve time (a callee spelled as a type
// word). Name lookup, imported-value handling, and `that`-clause rewriting
// stay in this one constructor.
AstNode* build_identifier_from_span(Transpiler* tp, SourceSpan span);

// Scope mutation is semantic state, not parser lookahead state. A committed
// recursive-descent branch enters through this pair before it constructs child
// expressions, and leaves through the exact returned scope.
NameScope* lambda_ast_enter_scope(Transpiler* tp, bool is_proc);
NameScope* lambda_ast_enter_scope_with_parent(Transpiler* tp,
        NameScope* parent, bool is_proc);
void lambda_ast_leave_scope(Transpiler* tp, NameScope* scope);

// Register a declaration once it has resolved. Forward placeholders use the
// same NameEntry path as their final declarations, so recursive references
// keep a single binding identity.
void lambda_ast_register_name(Transpiler* tp, AstNode* node);

// Call-boundary validation belongs to semantic construction. The parser only
// supplies a committed source span and already-built callee/argument nodes.
bool lambda_ast_validate_call_arguments(Transpiler* tp, AstCallNode* call,
        SourceSpan diagnostic_span, int arg_count);

// Collects the invocation-local contracts selected by a statically typed call
// to a binder-carrying function.  MIR uses this semantic result to form an
// exact raw-variant key; validation remains responsible for diagnostics.
bool lambda_ast_collect_static_binder_env(AstCallNode* call,
        Type** env_out, uint16_t env_count);

Type* declared_compound_destination_type(Transpiler* tp, AstNode* node,
    const char** destination_label);
// Builds and resolves a call at resolve time (a bare pipe system function
// promoted to a call), under the pipe state its caller set.
AstNode* build_call_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* function, AstNode* arguments, int arg_count);

// The two front-end phases. Parsing drives the syntax sink, which allocates
// every retained node as its production completes, with names unbound and
// typed `any`; a syntax error leaves the nodes built so far in the pool,
// never published. Resolving walks that tree in production order and binds,
// types and validates it into the AST the bind pass consumes.
typedef struct LambdaSyntaxUnit LambdaSyntaxUnit;
LambdaParseStatus lambda_rd_parse_syntax(Transpiler* tp, const char* source,
        size_t length, LambdaSyntaxUnit** unit_out, LambdaParseError* error);
LambdaParseStatus lambda_rd_resolve_syntax(Transpiler* tp,
        LambdaSyntaxUnit* unit, AstScript** root_out, LambdaParseError* error);
void lambda_rd_destroy_syntax(LambdaSyntaxUnit* unit);

// Resolve a Lambda source import through the same package/relative rules used
// by AST construction. The returned `.ls` path is caller-owned.
char* lambda_resolve_import_module_path(const char* base_directory,
    StrView module);

// Both phases back to back, for callers that need only the resolved tree.
LambdaParseStatus lambda_rd_reduce_ast(Transpiler* tp, const char* source,
        size_t length, AstScript** root_out, LambdaParseError* error);

// The resolve pass uses short-lived construction scopes for bottom-up type
// assembly. Rebuild the canonical lexical graph from the retained AST before
// validation or lowering publishes any of those edges.
bool lambda_ast_rebind_direct_scope_graph(Transpiler* tp, AstScript* script);
// The bind pass collects every function while it rewrites AST edges. Later
// validation analyses share this list instead of rescanning the whole unit.
bool lambda_ast_rebind_direct_scope_graph_with_functions(Transpiler* tp,
    AstScript* script, ArrayList** functions_out);

// Run post-reduction semantic validation and analysis for an already built AST.
bool lambda_ast_finalize_script(Transpiler* tp, AstScript* script);
bool lambda_ast_finalize_script_with_functions(Transpiler* tp,
    AstScript* script, ArrayList* functions);
