#pragma once

// Parser-neutral AST construction seams.
//
// The Tree-sitter walker and the first-party recursive-descent parser both
// commit operator spelling and child AST nodes before semantic construction.
// Keep that construction here so the two front ends do not grow independent
// type inference or diagnostic behavior.

#include "ast.hpp"

// Maps an already-committed Lambda operator spelling to the retained AST
// operator. Prefix and infix spellings intentionally use separate entry
// points: `+`, `-`, `*`, and `!` have different retained meanings by form.
bool lambda_unary_operator_from_spelling(StrView spelling, Operator* op_out);
bool lambda_binary_operator_from_spelling(StrView spelling, Operator* op_out);

// Resolves an identifier after the parser has committed its spelling. Name
// lookup, imported-value handling, and `that`-clause rewriting stay in this
// shared constructor rather than being reimplemented by the direct sink.
AstNode* build_identifier_from_span(Transpiler* tp, LambdaSourceSpan span);

// Contextual atoms are semantic rather than lexical: `~#` is the current
// index and `^` is valid only while a handler body is being constructed.
AstNode* build_current_item_from_span(Transpiler* tp, LambdaSourceSpan span,
        bool is_index);
AstNode* build_current_error_from_span(Transpiler* tp, LambdaSourceSpan span);
AstNode* build_current_parent_navigation_from_span(Transpiler* tp,
        LambdaSourceSpan span);

// A parenthesized Lambda expression remains an observable AST_NODE_PRIMARY;
// the direct front end must retain this wrapper instead of flattening it.
AstNode* build_primary_wrapper_from_parts(Transpiler* tp, LambdaSourceSpan span,
        AstNode* expr);

// Builds the ordinary unary semantic node after parsing has committed its
// operator and operand. The special spread/type-negation forms intentionally
// remain owned by their dedicated constructors.
AstNode* build_unary_node_from_parts(Transpiler* tp, LambdaSourceSpan span,
        StrView op_spelling, AstNode* operand);
