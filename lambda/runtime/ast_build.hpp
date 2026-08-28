#pragma once

// Parser-neutral AST construction seams.
//
// The first-party recursive-descent parser commits operator spelling and child
// AST nodes before semantic construction. Keep that construction here so the
// parser and semantic layer do not grow independent inference or diagnostics.

#include "ast.hpp"
#include "parser/lambda_rd_parser.h"

// Span allocation is the only AST allocation primitive the direct parser may
// use.
AstNode* alloc_ast_node_from_span(Transpiler* tp, AstNodeType node_type,
        SourceSpan span, size_t size);

// Move a direct-parser fragment's byte ranges into an append-only REPL source
// buffer. The parser receives only the new fragment, so its local offsets must
// be rebased before the fragment is attached to the session AST.
void lambda_ast_shift_source_spans(AstNode* root, uint32_t byte_offset);

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

// Builds the retained primary literal and its constant/type payload from the
// original source span. No literal parser may require a synthetic parser node.
AstNode* build_literal_from_span(Transpiler* tp, SourceSpan span,
        LambdaAstLiteralKind kind);

// Maps an already-committed Lambda operator spelling to the retained AST
// operator. Prefix and infix spellings intentionally use separate entry
// points: `+`, `-`, `*`, and `!` have different retained meanings by form.
bool lambda_unary_operator_from_spelling(StrView spelling, Operator* op_out);
bool lambda_binary_operator_from_spelling(StrView spelling, Operator* op_out);

// Resolves an identifier after the parser has committed its spelling. Name
// lookup, imported-value handling, and `that`-clause rewriting stay in this
// shared constructor rather than being reimplemented by the direct sink.
AstNode* build_identifier_from_span(Transpiler* tp, SourceSpan span);

// Scope mutation is semantic state, not parser lookahead state. A committed
// recursive-descent branch enters through this pair before it constructs child
// expressions, and leaves through the exact returned scope.
NameScope* lambda_ast_enter_scope(Transpiler* tp, bool is_proc);
NameScope* lambda_ast_enter_scope_with_parent(Transpiler* tp,
        NameScope* parent, bool is_proc);
void lambda_ast_leave_scope(Transpiler* tp, NameScope* scope);

// Register an already-built declaration only after its grammar branch has
// committed. Forward placeholders use the same NameEntry path as their final
// declarations, so recursive references keep a single binding identity.
void lambda_ast_register_name(Transpiler* tp, AstNode* node);
AstFuncNode* build_function_placeholder_from_parts(Transpiler* tp,
        SourceSpan span, StrView name, bool is_proc);

// Contextual atoms are semantic rather than lexical: `~#` is the current
// index and `^` is valid only while a handler body is being constructed.
AstNode* build_current_item_from_span(Transpiler* tp, SourceSpan span,
        bool is_index);
AstNode* build_current_error_from_span(Transpiler* tp, SourceSpan span);
AstNode* build_current_parent_navigation_from_span(Transpiler* tp,
        SourceSpan span);

// A parenthesized Lambda expression remains an observable AST_NODE_PRIMARY;
// the direct front end must retain this wrapper instead of flattening it.
AstNode* build_primary_wrapper_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* expr);

// Assemble already-constructed collection children. The direct sink supplies
// the ordered child list; shape/type inference remains here rather than being
// copied into the parser.
AstNode* build_array_from_items(Transpiler* tp, SourceSpan span,
        AstNode* items);
AstNode* build_map_from_items(Transpiler* tp, SourceSpan span,
        AstNode* items);

// Build a named call argument after the parser has committed its key/value.
AstNamedNode* build_named_argument_from_parts(Transpiler* tp,
        SourceSpan span, StrView name, AstNode* value);

AstDeclaratorNode* build_declarator_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* type_expr, AstNode* value);
AstNode* build_decompose_from_parts(Transpiler* tp, SourceSpan span,
        String** names, int name_count, AstNode* value, bool is_named);
AstNode* build_assignment_statement_from_parts(Transpiler* tp,
        SourceSpan span, AstNode* target, AstNode* value);

// Call-boundary validation belongs to semantic construction. The parser only
// supplies a committed source span and already-built callee/argument nodes.
bool lambda_ast_validate_call_arguments(Transpiler* tp, AstCallNode* call,
        SourceSpan diagnostic_span, int arg_count);

// Builds the ordinary unary semantic node after parsing has committed its
// operator and operand. The special spread/type-negation forms intentionally
// remain owned by their dedicated constructors.
AstNode* build_unary_node_from_parts(Transpiler* tp, SourceSpan span,
        StrView op_spelling, AstNode* operand);

AstNode* build_binary_node_from_parts(Transpiler* tp, SourceSpan span,
        StrView op_spelling, AstNode* left, AstNode* right);
AstNode* build_field_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNodeType node_type, AstNode* object, AstNode* field);
AstNode* build_navigation_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* object, bool root);
AstNode* build_query_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* object, AstNode* query, bool direct);
AstNode* build_call_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* function, AstNode* arguments, int arg_count);
AstNode* build_raise_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* value, bool statement_form);
AstNode* build_spread_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand);
AstNode* build_type_negation_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand);
AstNode* build_element_from_parts(Transpiler* tp, SourceSpan span,
        SourceSpan tag_span, AstNode* children);
AstNamedNode* build_param_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* type_expr, AstNode* default_value,
        bool optional, bool is_var);
AstNode* build_function_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* params, AstNode* returned, AstNode* error_type,
        AstNode* body, bool is_proc, bool variadic, bool raised);
AstNode* build_if_node_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* condition, AstNode* then_branch, AstNode* else_branch);
AstNode* build_match_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* scrutinee, AstNode* arms);
AstNode* build_handler_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* operand, AstNode* body, AstNode* value_body);
AstNode* build_loop_from_parts(Transpiler* tp, SourceSpan span,
    LambdaToken name_token, LambdaToken index_token, uint32_t flags,
    AstNode* index_type, AstNode* source, AstNode* join);
AstNode* build_for_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* clauses, AstNode* body, NameScope* loop_scope,
    bool statement_form);
AstNode* build_while_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* condition, AstNode* body, NameScope* loop_scope);
AstNode* build_propagate_node_from_parts(Transpiler* tp, SourceSpan span,
    AstNode* operand);

// Build one compilation unit directly from the recursive-descent/Pratt
// reduction stream.  The caller owns the Transpiler/AST pool; on success the
// returned root is safe to pass to the normal compiler pass manager.
LambdaParseStatus lambda_rd_build_ast(Transpiler* tp, const char* source,
        size_t length, AstScript** root_out, LambdaParseError* error);
