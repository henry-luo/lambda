// =============================================================================
// js_early_errors.cpp — Static semantic validation for JavaScript AST
// =============================================================================
// Walks the AST after parsing and before codegen to detect early errors
// that the spec requires to be SyntaxError (or ReferenceError) at parse time.
//
// Phases implemented:
//   1. Assignment target validation (invalid LHS in assignment/update)
//   2. Reserved word / keyword-as-identifier detection
//   3. Destructuring pattern validation (rest violations)
//   4. Block-scope redeclaration detection
//   5. Strict mode enforcement (duplicate params, octal, with)
//   6. Context-sensitive yield/await/super checks
// =============================================================================

#include "js_ast.hpp"
#include "js_transpiler.hpp"
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include <cstring>
#include <cstdio>

// context flags passed down during AST walk
struct EarlyErrorCtx {
    JsTranspiler* tp;
    bool in_generator;
    bool in_async;
    bool in_strict;          // "use strict" scope
    bool in_class_body;
    bool in_constructor;
    bool in_method;          // class method (non-constructor)
    bool in_static_init;     // class static block
    int  error_count;
};

// ---- helpers ---------------------------------------------------------------

static void ee_error(EarlyErrorCtx* ctx, JsAstNode* n, const char* fmt, ...) {
    ctx->error_count++;
    // use js_error which sets tp->has_errors and appends to error_buf
    // but we also need to print "SyntaxError" to stderr for test262 detection
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    js_error(ctx->tp, n->node, "%s", buf);

    uint32_t row = ts_node_start_point(n->node).row + 1;
    uint32_t col = ts_node_start_point(n->node).column + 1;
    fprintf(stderr, "SyntaxError: %s (at line %u, column %u)\n", buf, row, col);
}

// ---- reserved word tables --------------------------------------------------

static const char* JS_RESERVED_KEYWORDS[] = {
    "break", "case", "catch", "continue", "debugger", "default", "delete",
    "do", "else", "finally", "for", "function", "if", "in", "instanceof",
    "new", "return", "switch", "this", "throw", "try", "typeof", "var",
    "void", "while", "with",
    NULL
};

static const char* JS_STRICT_RESERVED[] = {
    "implements", "interface", "let", "package", "private", "protected",
    "public", "static", "yield",
    NULL
};

static const char* JS_FUTURE_RESERVED[] = {
    "enum",
    NULL
};

static bool is_in_list(const char* name, const char** list) {
    for (int i = 0; list[i]; i++) {
        if (strcmp(name, list[i]) == 0) return true;
    }
    return false;
}

static bool is_reserved_word(const char* name, bool strict) {
    if (is_in_list(name, JS_RESERVED_KEYWORDS)) return true;
    if (is_in_list(name, JS_FUTURE_RESERVED)) return true;
    if (strict && is_in_list(name, JS_STRICT_RESERVED)) return true;
    return false;
}

// ---- unicode escape normalization ------------------------------------------
// Resolve \uXXXX and \u{XXXX} escapes in an identifier name.
// Returns true if the name contains escapes and normalized differs from raw.
// The normalized result is written to `out` (caller must provide >=512 bytes).

static bool normalize_unicode_escapes(const char* src, int src_len, char* out, int out_size) {
    bool has_escapes = false;
    int oi = 0;
    for (int i = 0; i < src_len && oi < out_size - 1; ) {
        if (src[i] == '\\' && i + 1 < src_len && src[i+1] == 'u') {
            has_escapes = true;
            i += 2; // skip \u
            unsigned int codepoint = 0;
            if (i < src_len && src[i] == '{') {
                i++; // skip {
                while (i < src_len && src[i] != '}') {
                    char c = src[i++];
                    codepoint <<= 4;
                    if (c >= '0' && c <= '9') codepoint |= (c - '0');
                    else if (c >= 'a' && c <= 'f') codepoint |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') codepoint |= (c - 'A' + 10);
                }
                if (i < src_len && src[i] == '}') i++; // skip }
            } else {
                // \uXXXX — exactly 4 hex digits
                for (int j = 0; j < 4 && i < src_len; j++, i++) {
                    char c = src[i];
                    codepoint <<= 4;
                    if (c >= '0' && c <= '9') codepoint |= (c - '0');
                    else if (c >= 'a' && c <= 'f') codepoint |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') codepoint |= (c - 'A' + 10);
                }
            }
            // encode as UTF-8
            if (codepoint < 0x80) {
                out[oi++] = (char)codepoint;
            } else if (codepoint < 0x800) {
                if (oi + 1 < out_size) {
                    out[oi++] = (char)(0xC0 | (codepoint >> 6));
                    out[oi++] = (char)(0x80 | (codepoint & 0x3F));
                }
            } else if (codepoint < 0x10000) {
                if (oi + 2 < out_size) {
                    out[oi++] = (char)(0xE0 | (codepoint >> 12));
                    out[oi++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    out[oi++] = (char)(0x80 | (codepoint & 0x3F));
                }
            } else {
                if (oi + 3 < out_size) {
                    out[oi++] = (char)(0xF0 | (codepoint >> 18));
                    out[oi++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                    out[oi++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    out[oi++] = (char)(0x80 | (codepoint & 0x3F));
                }
            }
        } else {
            out[oi++] = src[i++];
        }
    }
    out[oi] = '\0';
    return has_escapes;
}

// ---- Phase 1: assignment target validation ---------------------------------

// Check whether an expression node is a valid simple assignment target.
// Valid targets: identifiers (not reserved words), member expressions,
// array/object patterns (destructuring).
static bool is_valid_assignment_target(JsAstNode* node, bool strict) {
    if (!node) return false;
    switch (node->node_type) {
        case JS_AST_NODE_IDENTIFIER: {
            JsIdentifierNode* id = (JsIdentifierNode*)node;
            if (id->name) {
                const char* nm = id->name->chars;
                // "eval" and "arguments" are invalid in strict mode
                if (strict && (strcmp(nm, "eval") == 0 || strcmp(nm, "arguments") == 0)) {
                    return false;
                }
                // reserved words are never valid assignment targets
                if (is_reserved_word(nm, strict)) return false;
            }
            return true;
        }
        case JS_AST_NODE_MEMBER_EXPRESSION:
            return true;
        case JS_AST_NODE_ARRAY_PATTERN:
        case JS_AST_NODE_OBJECT_PATTERN:
            return true;
        default:
            return false;
    }
}

static void check_assignment_target(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node) return;
    JsAssignmentNode* asgn = (JsAssignmentNode*)node;
    JsAstNode* lhs = asgn->left;
    if (!lhs) return;

    // for simple assignment (=), the LHS can be a pattern or simple target
    // for compound assignments (+=, etc.), LHS must be a simple target (no patterns)
    bool is_compound = (asgn->op != JS_OP_ASSIGN);

    if (is_compound) {
        // compound: only identifier or member expression
        if (lhs->node_type != JS_AST_NODE_IDENTIFIER &&
            lhs->node_type != JS_AST_NODE_MEMBER_EXPRESSION) {
            ee_error(ctx, node, "Invalid left-hand side in compound assignment");
        }
    } else {
        if (!is_valid_assignment_target(lhs, ctx->in_strict)) {
            ee_error(ctx, node, "Invalid left-hand side in assignment");
        }
    }
}

static void check_update_target(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node) return;
    JsUnaryNode* un = (JsUnaryNode*)node;
    if (un->op != JS_OP_INCREMENT && un->op != JS_OP_DECREMENT) return;
    JsAstNode* operand = un->operand;
    if (!operand) return;

    if (operand->node_type != JS_AST_NODE_IDENTIFIER &&
        operand->node_type != JS_AST_NODE_MEMBER_EXPRESSION) {
        ee_error(ctx, node, "Invalid left-hand side in prefix/postfix operation");
    } else if (operand->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)operand;
        if (id->name) {
            const char* nm = id->name->chars;
            if (ctx->in_strict && (strcmp(nm, "eval") == 0 || strcmp(nm, "arguments") == 0)) {
                ee_error(ctx, node, "Invalid left-hand side in prefix/postfix operation");
            }
        }
    }
}

// ---- Phase 2: reserved word as identifier ----------------------------------

// Check raw source text for unicode escapes in identifiers that resolve
// to reserved words. Tree-sitter resolves escapes in its AST, so the
// identifier name may be "break" when the source was "br\u0065ak".
static void check_identifier_reserved(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return;
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    if (!id->name) return;
    const char* name = id->name->chars;

    // check if the name itself is a reserved word
    if (is_reserved_word(name, ctx->in_strict)) {
        ee_error(ctx, node, "'%s' is a reserved word and cannot be used as an identifier", name);
        return;
    }

    // check via raw source text for unicode-escaped reserved words
    uint32_t start = ts_node_start_byte(node->node);
    uint32_t end = ts_node_end_byte(node->node);
    if (end > start && ctx->tp->source) {
        int slen = (int)(end - start);
        const char* raw = ctx->tp->source + start;
        // only check if it contains a backslash (escape sequence)
        bool has_backslash = false;
        for (int i = 0; i < slen; i++) {
            if (raw[i] == '\\') { has_backslash = true; break; }
        }
        if (has_backslash) {
            char normalized[512];
            if (normalize_unicode_escapes(raw, slen, normalized, sizeof(normalized))) {
                if (is_reserved_word(normalized, ctx->in_strict)) {
                    ee_error(ctx, node, "'%s' (via unicode escape) is a reserved word", normalized);
                }
            }
        }
    }
}

// ---- Phase 3: destructuring pattern validation -----------------------------

static void check_rest_element(EarlyErrorCtx* ctx, JsAstNode* node) {
    // rest element must be last in a pattern, must not have initializer
    // The AST builder puts rest as JS_AST_NODE_REST_ELEMENT in array patterns
    // We check at the array pattern parent level
}

static void check_array_pattern(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_ARRAY_PATTERN) return;
    JsArrayPatternNode* pat = (JsArrayPatternNode*)node;

    // walk elements — if rest is not last, error
    for (JsAstNode* elem = pat->elements; elem; elem = elem->next) {
        if (elem->node_type == JS_AST_NODE_REST_ELEMENT) {
            if (elem->next) {
                ee_error(ctx, elem, "Rest element must be last element in destructuring pattern");
            }
            // rest must not have default value
            JsSpreadElementNode* rest = (JsSpreadElementNode*)elem;
            if (rest->argument && rest->argument->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                ee_error(ctx, elem, "Rest element may not have a default initializer");
            }
        }
    }
}

static void check_object_pattern(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_OBJECT_PATTERN) return;
    JsObjectPatternNode* pat = (JsObjectPatternNode*)node;

    // walk properties — rest must be last
    for (JsAstNode* prop = pat->properties; prop; prop = prop->next) {
        if (prop->node_type == JS_AST_NODE_REST_ELEMENT || prop->node_type == JS_AST_NODE_REST_PROPERTY) {
            if (prop->next) {
                ee_error(ctx, prop, "Rest element must be last element in destructuring pattern");
            }
        }
    }
}

// ---- Phase 4: block-scope redeclaration ------------------------------------

struct BlockScopeEntry {
    const char* name;
    int kind; // JsVarKind
};

static uint64_t bse_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const BlockScopeEntry* e = (const BlockScopeEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

static int bse_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((const BlockScopeEntry*)a)->name, ((const BlockScopeEntry*)b)->name);
}

static void check_block_redeclarations(EarlyErrorCtx* ctx, JsAstNode* stmts) {
    // scan a block's statement list for duplicate let/const declarations
    struct hashmap* scope = hashmap_new(sizeof(BlockScopeEntry), 16, 0, 0, bse_hash, bse_cmp, NULL, NULL);

    for (JsAstNode* s = stmts; s; s = s->next) {
        if (s->node_type != JS_AST_NODE_VARIABLE_DECLARATION) continue;
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)s;
        int kind = vd->kind; // 0=var, 1=let, 2=const
        if (kind == JS_VAR_VAR) continue; // var has function scope, skip

        for (JsAstNode* decl = vd->declarations; decl; decl = decl->next) {
            if (decl->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* vdecl = (JsVariableDeclaratorNode*)decl;
            if (!vdecl->id || vdecl->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
            JsIdentifierNode* id = (JsIdentifierNode*)vdecl->id;
            if (!id->name) continue;

            const char* name = id->name->chars;
            BlockScopeEntry probe = {name, 0};
            const BlockScopeEntry* existing = (const BlockScopeEntry*)hashmap_get(scope, &probe);
            if (existing) {
                ee_error(ctx, (JsAstNode*)id,
                    "Identifier '%s' has already been declared in this scope", name);
            } else {
                BlockScopeEntry entry = {name, kind};
                hashmap_set(scope, &entry);
            }
        }

        // also check for class and function declarations in the same scope
    }

    // also check for class declarations
    for (JsAstNode* s = stmts; s; s = s->next) {
        if (s->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            JsClassNode* cls = (JsClassNode*)s;
            if (cls->name) {
                const char* name = cls->name->chars;
                BlockScopeEntry probe = {name, 0};
                const BlockScopeEntry* existing = (const BlockScopeEntry*)hashmap_get(scope, &probe);
                if (existing) {
                    ee_error(ctx, s, "Identifier '%s' has already been declared in this scope", name);
                } else {
                    BlockScopeEntry entry = {name, JS_VAR_CONST};
                    hashmap_set(scope, &entry);
                }
            }
        }
    }

    hashmap_free(scope);
}

// ---- Phase 5: strict mode --------------------------------------------------

static bool detect_strict_mode(JsAstNode* body) {
    // check if first statement is "use strict" string literal expression
    if (!body) return false;
    JsAstNode* first = NULL;
    if (body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        first = ((JsBlockNode*)body)->statements;
    } else {
        first = body; // linked list of statements
    }
    if (!first || first->node_type != JS_AST_NODE_EXPRESSION_STATEMENT) return false;
    JsExpressionStatementNode* es = (JsExpressionStatementNode*)first;
    if (!es->expression || es->expression->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)es->expression;
    if (lit->literal_type != JS_LITERAL_STRING) return false;
    if (lit->value.string_value && strcmp(lit->value.string_value->chars, "use strict") == 0) {
        return true;
    }
    return false;
}

static void check_duplicate_params(EarlyErrorCtx* ctx, JsFunctionNode* func) {
    // in strict mode or with default/rest/destructuring params, duplicate params are illegal
    if (!func->params) return;

    // detect if function has non-simple parameters
    bool has_non_simple = false;
    for (JsAstNode* p = func->params; p; p = p->next) {
        if (p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
            p->node_type == JS_AST_NODE_REST_ELEMENT ||
            p->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            p->node_type == JS_AST_NODE_OBJECT_PATTERN) {
            has_non_simple = true;
            break;
        }
    }

    if (!ctx->in_strict && !has_non_simple) return; // sloppy mode with simple params allows dupes

    // collect param names
    const char* names[256];
    int count = 0;
    for (JsAstNode* p = func->params; p; p = p->next) {
        JsAstNode* target = p;
        if (p->node_type == JS_AST_NODE_PARAMETER) {
            // JsParameter wraps the actual pattern, but we just need the identifier
            // Check if it has a name child
        }
        if (target->node_type == JS_AST_NODE_IDENTIFIER && count < 256) {
            JsIdentifierNode* id = (JsIdentifierNode*)target;
            if (id->name) {
                // check for duplicate
                for (int i = 0; i < count; i++) {
                    if (strcmp(names[i], id->name->chars) == 0) {
                        ee_error(ctx, target, "Duplicate parameter name '%s' not allowed", id->name->chars);
                        break;
                    }
                }
                names[count++] = id->name->chars;
            }
        }
    }
}

// ---- recursive AST walker --------------------------------------------------

static void walk_expression(EarlyErrorCtx* ctx, JsAstNode* node);
static void walk_statement(EarlyErrorCtx* ctx, JsAstNode* node);
static void walk_statements(EarlyErrorCtx* ctx, JsAstNode* stmts);

static void walk_expression(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node) return;

    switch (node->node_type) {
        case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
            check_assignment_target(ctx, node);
            walk_expression(ctx, ((JsAssignmentNode*)node)->left);
            walk_expression(ctx, ((JsAssignmentNode*)node)->right);
            break;

        case JS_AST_NODE_UNARY_EXPRESSION: {
            JsUnaryNode* un = (JsUnaryNode*)node;
            if (un->op == JS_OP_INCREMENT || un->op == JS_OP_DECREMENT) {
                check_update_target(ctx, node);
            }
            walk_expression(ctx, un->operand);
            break;
        }

        case JS_AST_NODE_IDENTIFIER:
            // Reserved word checks are done in binding positions (declarations,
            // parameters), not in expression (reference) positions. Keywords
            // like 'this', 'null', 'true' may appear as identifiers in the AST.
            break;

        case JS_AST_NODE_BINARY_EXPRESSION: {
            JsBinaryNode* bn = (JsBinaryNode*)node;
            walk_expression(ctx, bn->left);
            walk_expression(ctx, bn->right);
            break;
        }

        case JS_AST_NODE_CALL_EXPRESSION: {
            JsCallNode* cn = (JsCallNode*)node;
            walk_expression(ctx, cn->callee);
            for (JsAstNode* a = cn->arguments; a; a = a->next)
                walk_expression(ctx, a);
            break;
        }

        case JS_AST_NODE_MEMBER_EXPRESSION: {
            JsMemberNode* mn = (JsMemberNode*)node;
            walk_expression(ctx, mn->object);
            if (mn->computed) walk_expression(ctx, mn->property);
            break;
        }

        case JS_AST_NODE_ARRAY_EXPRESSION: {
            JsArrayNode* an = (JsArrayNode*)node;
            for (JsAstNode* e = an->elements; e; e = e->next)
                walk_expression(ctx, e);
            break;
        }

        case JS_AST_NODE_OBJECT_EXPRESSION: {
            JsObjectNode* on = (JsObjectNode*)node;
            for (JsAstNode* p = on->properties; p; p = p->next) {
                if (p->node_type == JS_AST_NODE_PROPERTY) {
                    JsPropertyNode* prop = (JsPropertyNode*)p;
                    walk_expression(ctx, prop->value);
                } else if (p->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                    walk_expression(ctx, ((JsSpreadElementNode*)p)->argument);
                }
            }
            break;
        }

        case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
            JsConditionalNode* cn = (JsConditionalNode*)node;
            walk_expression(ctx, cn->test);
            walk_expression(ctx, cn->consequent);
            walk_expression(ctx, cn->alternate);
            break;
        }

        case JS_AST_NODE_SEQUENCE_EXPRESSION: {
            JsSequenceNode* sn = (JsSequenceNode*)node;
            for (JsAstNode* e = sn->expressions; e; e = e->next)
                walk_expression(ctx, e);
            break;
        }

        case JS_AST_NODE_TEMPLATE_LITERAL: {
            JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
            for (JsAstNode* e = tl->expressions; e; e = e->next)
                walk_expression(ctx, e);
            break;
        }

        case JS_AST_NODE_SPREAD_ELEMENT:
            walk_expression(ctx, ((JsSpreadElementNode*)node)->argument);
            break;

        case JS_AST_NODE_YIELD_EXPRESSION:
            walk_expression(ctx, ((JsYieldNode*)node)->argument);
            break;

        case JS_AST_NODE_AWAIT_EXPRESSION:
            walk_expression(ctx, ((JsAwaitNode*)node)->argument);
            break;

        case JS_AST_NODE_ARROW_FUNCTION:
        case JS_AST_NODE_FUNCTION_EXPRESSION: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            bool was_gen = ctx->in_generator;
            bool was_async = ctx->in_async;
            bool was_strict = ctx->in_strict;
            ctx->in_generator = fn->is_generator;
            ctx->in_async = fn->is_async;

            // detect strict mode in function body
            if (fn->body && detect_strict_mode(fn->body)) {
                ctx->in_strict = true;
            }

            check_duplicate_params(ctx, fn);

            // walk params
            for (JsAstNode* p = fn->params; p; p = p->next) {
                walk_expression(ctx, p);
            }

            // walk body
            walk_statement(ctx, fn->body);

            ctx->in_generator = was_gen;
            ctx->in_async = was_async;
            ctx->in_strict = was_strict;
            break;
        }

        case JS_AST_NODE_CLASS_EXPRESSION: {
            JsClassNode* cls = (JsClassNode*)node;
            bool was_strict = ctx->in_strict;
            ctx->in_strict = true; // class bodies are always strict
            walk_expression(ctx, cls->superclass);
            for (JsAstNode* m = cls->body; m; m = m->next) {
                walk_statement(ctx, m);
            }
            ctx->in_strict = was_strict;
            break;
        }

        case JS_AST_NODE_NEW_EXPRESSION: {
            JsCallNode* ne = (JsCallNode*)node;
            walk_expression(ctx, ne->callee);
            for (JsAstNode* a = ne->arguments; a; a = a->next)
                walk_expression(ctx, a);
            break;
        }

        case JS_AST_NODE_ASSIGNMENT_PATTERN: {
            JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)node;
            walk_expression(ctx, ap->left);
            walk_expression(ctx, ap->right);
            break;
        }

        case JS_AST_NODE_ARRAY_PATTERN:
            check_array_pattern(ctx, node);
            for (JsAstNode* e = ((JsArrayPatternNode*)node)->elements; e; e = e->next)
                walk_expression(ctx, e);
            break;

        case JS_AST_NODE_OBJECT_PATTERN:
            check_object_pattern(ctx, node);
            for (JsAstNode* p = ((JsObjectPatternNode*)node)->properties; p; p = p->next) {
                if (p->node_type == JS_AST_NODE_PROPERTY) {
                    walk_expression(ctx, ((JsPropertyNode*)p)->value);
                } else {
                    walk_expression(ctx, p);
                }
            }
            break;

        case JS_AST_NODE_REST_ELEMENT:
        case JS_AST_NODE_REST_PROPERTY:
            walk_expression(ctx, ((JsSpreadElementNode*)node)->argument);
            break;

        default:
            break;
    }
}

static void walk_statement(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node) return;

    switch (node->node_type) {
        case JS_AST_NODE_BLOCK_STATEMENT: {
            JsBlockNode* blk = (JsBlockNode*)node;
            check_block_redeclarations(ctx, blk->statements);
            walk_statements(ctx, blk->statements);
            break;
        }

        case JS_AST_NODE_EXPRESSION_STATEMENT: {
            JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
            walk_expression(ctx, es->expression);
            break;
        }

        case JS_AST_NODE_VARIABLE_DECLARATION: {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
            for (JsAstNode* d = vd->declarations; d; d = d->next) {
                if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* vdecl = (JsVariableDeclaratorNode*)d;
                    // check binding identifier
                    if (vdecl->id) {
                        if (vdecl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                            check_identifier_reserved(ctx, vdecl->id);
                        } else {
                            walk_expression(ctx, vdecl->id);
                        }
                    }
                    walk_expression(ctx, vdecl->init);
                }
            }
            break;
        }

        case JS_AST_NODE_IF_STATEMENT: {
            JsIfNode* ifn = (JsIfNode*)node;
            walk_expression(ctx, ifn->test);
            walk_statement(ctx, ifn->consequent);
            walk_statement(ctx, ifn->alternate);
            break;
        }

        case JS_AST_NODE_WHILE_STATEMENT: {
            JsWhileNode* wn = (JsWhileNode*)node;
            walk_expression(ctx, wn->test);
            walk_statement(ctx, wn->body);
            break;
        }

        case JS_AST_NODE_DO_WHILE_STATEMENT: {
            JsDoWhileNode* dn = (JsDoWhileNode*)node;
            walk_statement(ctx, dn->body);
            walk_expression(ctx, dn->test);
            break;
        }

        case JS_AST_NODE_FOR_STATEMENT: {
            JsForNode* fn = (JsForNode*)node;
            walk_statement(ctx, fn->init); // may be var decl
            walk_expression(ctx, fn->init); // may be expression
            walk_expression(ctx, fn->test);
            walk_expression(ctx, fn->update);
            walk_statement(ctx, fn->body);
            break;
        }

        case JS_AST_NODE_FOR_OF_STATEMENT:
        case JS_AST_NODE_FOR_IN_STATEMENT: {
            JsForOfNode* fo = (JsForOfNode*)node;
            walk_statement(ctx, fo->left); // may be var decl
            walk_expression(ctx, fo->left); // may be pattern
            walk_expression(ctx, fo->right);
            walk_statement(ctx, fo->body);
            break;
        }

        case JS_AST_NODE_RETURN_STATEMENT:
            walk_expression(ctx, ((JsReturnNode*)node)->argument);
            break;

        case JS_AST_NODE_THROW_STATEMENT:
            walk_expression(ctx, ((JsThrowNode*)node)->argument);
            break;

        case JS_AST_NODE_TRY_STATEMENT: {
            JsTryNode* tn = (JsTryNode*)node;
            walk_statement(ctx, tn->block);
            if (tn->handler) {
                JsCatchNode* cn = (JsCatchNode*)tn->handler;
                walk_expression(ctx, cn->param);
                walk_statement(ctx, cn->body);
            }
            walk_statement(ctx, tn->finalizer);
            break;
        }

        case JS_AST_NODE_SWITCH_STATEMENT: {
            JsSwitchNode* sw = (JsSwitchNode*)node;
            walk_expression(ctx, sw->discriminant);
            for (JsAstNode* c = sw->cases; c; c = c->next) {
                if (c->node_type == JS_AST_NODE_SWITCH_CASE) {
                    JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
                    walk_expression(ctx, sc->test);
                    walk_statements(ctx, sc->consequent);
                }
            }
            break;
        }

        case JS_AST_NODE_FUNCTION_DECLARATION: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            bool was_gen = ctx->in_generator;
            bool was_async = ctx->in_async;
            bool was_strict = ctx->in_strict;
            ctx->in_generator = fn->is_generator;
            ctx->in_async = fn->is_async;

            if (fn->body && detect_strict_mode(fn->body)) {
                ctx->in_strict = true;
            }

            check_duplicate_params(ctx, fn);

            for (JsAstNode* p = fn->params; p; p = p->next) {
                walk_expression(ctx, p);
            }
            walk_statement(ctx, fn->body);

            ctx->in_generator = was_gen;
            ctx->in_async = was_async;
            ctx->in_strict = was_strict;
            break;
        }

        case JS_AST_NODE_CLASS_DECLARATION: {
            JsClassNode* cls = (JsClassNode*)node;
            bool was_strict = ctx->in_strict;
            ctx->in_strict = true; // class body is always strict
            walk_expression(ctx, cls->superclass);
            for (JsAstNode* m = cls->body; m; m = m->next) {
                walk_statement(ctx, m);
            }
            ctx->in_strict = was_strict;
            break;
        }

        case JS_AST_NODE_METHOD_DEFINITION: {
            JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)node;
            walk_expression(ctx, md->value);
            break;
        }

        case JS_AST_NODE_FIELD_DEFINITION: {
            JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)node;
            walk_expression(ctx, fd->value);
            break;
        }

        case JS_AST_NODE_LABELED_STATEMENT: {
            JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
            walk_statement(ctx, ls->body);
            break;
        }

        default:
            // for any other statement that wraps an expression:
            break;
    }
}

static void walk_statements(EarlyErrorCtx* ctx, JsAstNode* stmts) {
    for (JsAstNode* s = stmts; s; s = s->next) {
        walk_statement(ctx, s);
    }
}

// ---- public API ------------------------------------------------------------

// Returns the number of early errors found. Sets tp->has_errors if > 0.
int js_check_early_errors(JsTranspiler* tp, JsAstNode* ast) {
    if (!tp || !ast) return 0;

    EarlyErrorCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tp = tp;
    ctx.in_strict = tp->strict_mode;

    // for programs, check top-level "use strict"
    if (ast->node_type == JS_AST_NODE_PROGRAM) {
        JsProgramNode* prog = (JsProgramNode*)ast;
        if (detect_strict_mode(prog->body)) {
            ctx.in_strict = true;
        }
        check_block_redeclarations(&ctx, prog->body);
        walk_statements(&ctx, prog->body);
    } else {
        walk_statement(&ctx, ast);
    }

    return ctx.error_count;
}
