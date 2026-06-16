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
#include "js_runtime.h"
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include "../lib/utf.h"
#include <cstring>
#include <cstdio>

extern "C" Item js_eval_private_resolve(Item unscoped_key);

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
    bool in_formal_parameters;
    int  error_count;

    const char* private_names[128];
    int private_name_lens[128];
    int private_name_count;

    // Label tracking for continue target validation (ContainsUndefinedContinueTarget)
    // iteration_labels: labels attached to iteration statements (for/while/do-while/for-in/for-of)
    // non_iteration_labels: labels attached to non-iteration statements
    const char* iteration_labels[32];
    int iteration_label_lens[32];
    int iteration_label_count;
    bool in_iteration;       // currently inside any iteration statement
    bool in_switch;          // currently inside switch

    // All labels in scope (for break target validation per ContainsUndefinedBreakTarget).
    // A labeled break may target any enclosing labeled statement, not only iterations.
    const char* all_labels[64];
    int all_label_lens[64];
    int all_label_count;
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

static bool is_private_name_string(String* name) {
    return name && name->len > 10 && strncmp(name->chars, "__private_", 10) == 0;
}

static bool private_names_same_suffix(String* name, const char* declared, int declared_len) {
    if (!is_private_name_string(name) || !declared || declared_len <= 10) return false;
    int name_suffix_len = name->len - 10;
    int declared_suffix_len = declared_len - 10;
    if (name_suffix_len != declared_suffix_len) return false;
    return strncmp(name->chars + 10, declared + 10, name_suffix_len) == 0;
}

static bool ctx_private_name_is_declared(EarlyErrorCtx* ctx, String* name) {
    if (!is_private_name_string(name)) return true;
    for (int i = ctx->private_name_count - 1; i >= 0; i--) {
        if (private_names_same_suffix(name, ctx->private_names[i], ctx->private_name_lens[i])) return true;
    }
    Item resolved = js_eval_private_resolve((Item){.item = s2it(name)});
    if (resolved.item != ItemNull.item) return true;
    return false;
}

static void ctx_add_private_name(EarlyErrorCtx* ctx, String* name) {
    if (!is_private_name_string(name) || ctx->private_name_count >= 128) return;
    for (int i = ctx->private_name_count - 1; i >= 0; i--) {
        if (private_names_same_suffix(name, ctx->private_names[i], ctx->private_name_lens[i])) return;
    }
    ctx->private_names[ctx->private_name_count] = name->chars;
    ctx->private_name_lens[ctx->private_name_count] = name->len;
    ctx->private_name_count++;
}

static void collect_class_private_names(EarlyErrorCtx* ctx, JsClassNode* cls) {
    if (!ctx || !cls) return;
    JsAstNode* members = cls->body;
    if (members && members->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        members = ((JsBlockNode*)members)->statements;
    }
    for (JsAstNode* m = members; m; m = m->next) {
        if (m->node_type == JS_AST_NODE_METHOD_DEFINITION) {
            JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)m;
            if (!md->computed && md->key && md->key->node_type == JS_AST_NODE_IDENTIFIER) {
                ctx_add_private_name(ctx, ((JsIdentifierNode*)md->key)->name);
            }
        } else if (m->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)m;
            if (!fd->computed && fd->key && fd->key->node_type == JS_AST_NODE_IDENTIFIER) {
                ctx_add_private_name(ctx, ((JsIdentifierNode*)fd->key)->name);
            }
        }
    }
}

static void check_private_identifier_valid(EarlyErrorCtx* ctx, JsAstNode* node, String* name) {
    if (!is_private_name_string(name)) return;
    if (!ctx_private_name_is_declared(ctx, name)) {
        ee_error(ctx, node, "Private identifier '#%.*s' must be declared in an enclosing class",
            name->len - 10, name->chars + 10);
    }
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
            if (oi + 4 < out_size)
                oi += (int)utf8_encode(codepoint, out + oi);
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
        case JS_AST_NODE_CALL_EXPRESSION:
            return !strict;
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
            lhs->node_type != JS_AST_NODE_MEMBER_EXPRESSION &&
            (ctx->in_strict || lhs->node_type != JS_AST_NODE_CALL_EXPRESSION)) {
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
        operand->node_type != JS_AST_NODE_MEMBER_EXPRESSION &&
        (ctx->in_strict || operand->node_type != JS_AST_NODE_CALL_EXPRESSION)) {
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
    if (js_identifier_counters_is_enabled()) {
        js_identifier_counters_record_early_check();
    }

    // check if the name itself is a reserved word
    if (is_reserved_word(name, ctx->in_strict)) {
        ee_error(ctx, node, "'%s' is a reserved word and cannot be used as an identifier", name);
        return;
    }

    if (ctx->in_async && strcmp(name, "await") == 0) {
        ee_error(ctx, node, "'await' cannot be used as an identifier in async functions");
        return;
    }
    if (ctx->in_generator && strcmp(name, "yield") == 0) {
        ee_error(ctx, node, "'yield' cannot be used as an identifier in generator functions");
        return;
    }

    // v17: eval/arguments as binding names in strict mode
    if (ctx->in_strict) {
        if (strcmp(name, "eval") == 0 || strcmp(name, "arguments") == 0) {
            ee_error(ctx, node, "'%s' cannot be used as a binding name in strict mode", name);
            return;
        }
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
            bool reserved_hit = false;
            bool contextual_hit = false;
            bool normalized_ok = normalize_unicode_escapes(raw, slen, normalized, sizeof(normalized));
            if (normalized_ok) {
                reserved_hit = is_reserved_word(normalized, ctx->in_strict);
                contextual_hit = !reserved_hit &&
                    (strcmp(normalized, "await") == 0 || strcmp(normalized, "yield") == 0);
            }
            js_identifier_counters_record_early_escape(normalized_ok ? 1 : 0,
                reserved_hit ? 1 : 0, contextual_hit ? 1 : 0);
            if (normalized_ok) {
                if (reserved_hit) {
                    ee_error(ctx, node, "'%s' (via unicode escape) is a reserved word", normalized);
                }
                // P7b: contextually-reserved keywords ('await' / 'yield') must
                // also not be written with escape sequences. The spec is
                // explicit that escapes don't satisfy IdentifierName-not-
                // ReservedWord: even in a non-strict / non-async / non-module
                // context, no parse of `await` may flow into AwaitExpression
                // / IdentifierReference. Treat the escape form as a hard
                // SyntaxError so test262's `early-no-escaped-await.js` /
                // `early-no-escaped-yield.js` family stops crashing.
                else if (contextual_hit) {
                    ee_error(ctx, node, "'%s' may not contain escape sequences", normalized);
                }
            }
        }
    }
}

// ---- Phase 3: destructuring pattern validation -----------------------------

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

// v17: check if a function has non-simple parameters (defaults, rest, destructuring)
static bool has_non_simple_params(JsFunctionNode* func) {
    for (JsAstNode* p = func->params; p; p = p->next) {
        if (p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
            p->node_type == JS_AST_NODE_REST_ELEMENT ||
            p->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            p->node_type == JS_AST_NODE_OBJECT_PATTERN) {
            return true;
        }
    }
    return false;
}

// v17: "use strict" in function with non-simple params is SyntaxError
static void check_strict_non_simple(EarlyErrorCtx* ctx, JsFunctionNode* func) {
    if (!func->body) return;
    if (!func->has_use_strict_directive) return;
    if (has_non_simple_params(func)) {
        ee_error(ctx, (JsAstNode*)func,
            "Illegal 'use strict' directive in function with non-simple parameter list");
    }
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

static void check_binding_pattern_reserved(EarlyErrorCtx* ctx, JsAstNode* node) {
    if (!node) return;
    switch (node->node_type) {
        case JS_AST_NODE_IDENTIFIER:
            check_identifier_reserved(ctx, node);
            break;
        case JS_AST_NODE_ASSIGNMENT_PATTERN:
            check_binding_pattern_reserved(ctx, ((JsAssignmentPatternNode*)node)->left);
            break;
        case JS_AST_NODE_REST_ELEMENT:
        case JS_AST_NODE_REST_PROPERTY:
            check_binding_pattern_reserved(ctx, ((JsSpreadElementNode*)node)->argument);
            break;
        case JS_AST_NODE_ARRAY_PATTERN:
            for (JsAstNode* e = ((JsArrayPatternNode*)node)->elements; e; e = e->next)
                check_binding_pattern_reserved(ctx, e);
            break;
        case JS_AST_NODE_OBJECT_PATTERN:
            for (JsAstNode* p = ((JsObjectPatternNode*)node)->properties; p; p = p->next) {
                if (p->node_type == JS_AST_NODE_PROPERTY) {
                    check_binding_pattern_reserved(ctx, ((JsPropertyNode*)p)->value);
                } else {
                    check_binding_pattern_reserved(ctx, p);
                }
            }
            break;
        default:
            break;
    }
}

static void check_function_name_reserved(EarlyErrorCtx* ctx, JsFunctionNode* func) {
    if (!ctx->in_strict || !func || !func->name) return;
    if (strcmp(ts_node_type(func->base.node), "method_definition") == 0) return;
    const char* name = func->name->chars;
    if (strcmp(name, "eval") == 0 || strcmp(name, "arguments") == 0) {
        ee_error(ctx, (JsAstNode*)func, "'%s' cannot be used as a function name in strict mode", name);
    }
}

static bool regex_pattern_has_line_terminator(const char* pattern, int pattern_len) {
    if (!pattern || pattern_len <= 0) return false;
    for (int i = 0; i < pattern_len; i++) {
        unsigned char ch = (unsigned char)pattern[i];
        if (ch == '\n' || ch == '\r') return true;
        if (ch == 0xE2 && i + 2 < pattern_len &&
            (unsigned char)pattern[i + 1] == 0x80 &&
            ((unsigned char)pattern[i + 2] == 0xA8 ||
             (unsigned char)pattern[i + 2] == 0xA9)) {
            return true;
        }
    }
    return false;
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
            // v17: delete <identifier> is SyntaxError in strict mode
            if (un->op == JS_OP_DELETE && ctx->in_strict && un->operand &&
                un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
                ee_error(ctx, node, "Deleting a variable is not allowed in strict mode");
            }
            walk_expression(ctx, un->operand);
            break;
        }

        case JS_AST_NODE_IDENTIFIER:
            // Reserved word checks are done in binding positions (declarations,
            // parameters), not in expression (reference) positions. Keywords
            // like 'this', 'null', 'true' may appear as identifiers in the AST.
            check_private_identifier_valid(ctx, node, ((JsIdentifierNode*)node)->name);
            break;

        case JS_AST_NODE_LITERAL: {
            // v17: check for legacy octal literals in strict mode
            if (ctx->in_strict) {
                JsLiteralNode* lit = (JsLiteralNode*)node;
                if (lit->literal_type == JS_LITERAL_NUMBER && ctx->tp->source) {
                    uint32_t start = ts_node_start_byte(node->node);
                    uint32_t end = ts_node_end_byte(node->node);
                    int slen = (int)(end - start);
                    if (slen >= 2) {
                        const char* raw = ctx->tp->source + start;
                        // legacy octal: starts with 0 followed by digits 0-7 (not 0x, 0o, 0b, 0.)
                        if (raw[0] == '0' && slen >= 2 && raw[1] >= '0' && raw[1] <= '7') {
                            ee_error(ctx, node, "Octal literals are not allowed in strict mode");
                        }
                    }
                }
                if (lit->literal_type == JS_LITERAL_STRING && ctx->tp->source) {
                    uint32_t start = ts_node_start_byte(node->node);
                    uint32_t end = ts_node_end_byte(node->node);
                    int slen = (int)(end - start);
                    const char* raw = ctx->tp->source + start;
                    // check for octal escapes \0-\7 in string literals
                    for (int i = 0; i < slen - 1; i++) {
                        if (raw[i] == '\\') {
                            if (raw[i+1] == '\\') {
                                i++; // skip escaped backslash
                                continue;
                            }
                            if (raw[i+1] >= '1' && raw[i+1] <= '7') {
                                ee_error(ctx, node, "Octal escape sequences are not allowed in strict mode");
                                break;
                            }
                            if (raw[i+1] == '0' && i + 2 < slen &&
                                raw[i+2] >= '0' && raw[i+2] <= '9') {
                                ee_error(ctx, node, "Octal escape sequences are not allowed in strict mode");
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }

        case JS_AST_NODE_REGEX: {
            JsRegexNode* re = (JsRegexNode*)node;
            if (regex_pattern_has_line_terminator(re->pattern, re->pattern_len)) {
                ee_error(ctx, node, "Regular expression literal cannot contain a line terminator");
            }
            break;
        }

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
            if (mn->computed) {
                walk_expression(ctx, mn->property);
            } else if (mn->property && mn->property->node_type == JS_AST_NODE_IDENTIFIER) {
                check_private_identifier_valid(ctx, mn->property,
                    ((JsIdentifierNode*)mn->property)->name);
            }
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

        case JS_AST_NODE_TAGGED_TEMPLATE: {
            JsTaggedTemplateNode* tt = (JsTaggedTemplateNode*)node;
            walk_expression(ctx, tt->tag);
            if (tt->quasi) {
                for (JsAstNode* e = tt->quasi->expressions; e; e = e->next)
                    walk_expression(ctx, e);
            }
            break;
        }

        case JS_AST_NODE_SPREAD_ELEMENT:
            walk_expression(ctx, ((JsSpreadElementNode*)node)->argument);
            break;

        case JS_AST_NODE_YIELD_EXPRESSION:
            if (ctx->in_generator && ctx->in_formal_parameters) {
                ee_error(ctx, node, "YieldExpression is not permitted in generator formal parameters");
            }
            walk_expression(ctx, ((JsYieldNode*)node)->argument);
            break;

        case JS_AST_NODE_AWAIT_EXPRESSION:
            if (ctx->in_async && ctx->in_formal_parameters) {
                ee_error(ctx, node, "AwaitExpression is not permitted in async formal parameters");
            }
            walk_expression(ctx, ((JsAwaitNode*)node)->argument);
            break;

        case JS_AST_NODE_ARROW_FUNCTION:
        case JS_AST_NODE_FUNCTION_EXPRESSION:
        case JS_AST_NODE_FUNCTION_DECLARATION: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            bool was_gen = ctx->in_generator;
            bool was_async = ctx->in_async;
            bool was_strict = ctx->in_strict;
            // labels and iteration/switch context do not cross function boundaries
            bool was_iteration = ctx->in_iteration;
            bool was_switch = ctx->in_switch;
            int was_label_count = ctx->iteration_label_count;
            int was_all_label_count = ctx->all_label_count;
            ctx->in_generator = fn->is_generator;
            ctx->in_async = fn->is_async;
            ctx->in_iteration = false;
            ctx->in_switch = false;
            ctx->iteration_label_count = 0;
            ctx->all_label_count = 0;

            // v17: "use strict" with non-simple params is SyntaxError
            check_strict_non_simple(ctx, fn);

            // detect strict mode in function body
            if (fn->has_use_strict_directive) {
                ctx->in_strict = true;
            }

            check_duplicate_params(ctx, fn);
            check_function_name_reserved(ctx, fn);

            // walk params
            bool was_params = ctx->in_formal_parameters;
            ctx->in_formal_parameters = true;
            for (JsAstNode* p = fn->params; p; p = p->next) {
                check_binding_pattern_reserved(ctx, p);
                walk_expression(ctx, p);
            }
            ctx->in_formal_parameters = was_params;

            // walk body
            walk_statement(ctx, fn->body);

            ctx->in_generator = was_gen;
            ctx->in_async = was_async;
            ctx->in_strict = was_strict;
            ctx->in_iteration = was_iteration;
            ctx->in_switch = was_switch;
            ctx->iteration_label_count = was_label_count;
            ctx->all_label_count = was_all_label_count;
            break;
        }

        case JS_AST_NODE_CLASS_EXPRESSION:
        case JS_AST_NODE_CLASS_DECLARATION: {
            JsClassNode* cls = (JsClassNode*)node;
            bool was_strict = ctx->in_strict;
            int saved_private_count = ctx->private_name_count;
            ctx->in_strict = true; // class bodies are always strict
            collect_class_private_names(ctx, cls);
            walk_expression(ctx, cls->superclass);
            for (JsAstNode* m = cls->body; m; m = m->next) {
                walk_statement(ctx, m);
            }
            ctx->private_name_count = saved_private_count;
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
            bool saved_in_iteration = ctx->in_iteration;
            ctx->in_iteration = true;
            walk_statement(ctx, wn->body);
            ctx->in_iteration = saved_in_iteration;
            break;
        }

        case JS_AST_NODE_DO_WHILE_STATEMENT: {
            JsDoWhileNode* dn = (JsDoWhileNode*)node;
            bool saved_in_iteration = ctx->in_iteration;
            ctx->in_iteration = true;
            walk_statement(ctx, dn->body);
            ctx->in_iteration = saved_in_iteration;
            walk_expression(ctx, dn->test);
            break;
        }

        case JS_AST_NODE_FOR_STATEMENT: {
            JsForNode* fn = (JsForNode*)node;
            walk_statement(ctx, fn->init); // may be var decl
            walk_expression(ctx, fn->init); // may be expression
            walk_expression(ctx, fn->test);
            walk_expression(ctx, fn->update);
            bool saved_in_iteration = ctx->in_iteration;
            ctx->in_iteration = true;
            walk_statement(ctx, fn->body);
            ctx->in_iteration = saved_in_iteration;
            break;
        }

        case JS_AST_NODE_FOR_OF_STATEMENT:
        case JS_AST_NODE_FOR_IN_STATEMENT: {
            JsForOfNode* fo = (JsForOfNode*)node;
            walk_statement(ctx, fo->left); // may be var decl
            walk_expression(ctx, fo->left); // may be pattern
            walk_expression(ctx, fo->right);
            bool saved_in_iteration = ctx->in_iteration;
            ctx->in_iteration = true;
            walk_statement(ctx, fo->body);
            ctx->in_iteration = saved_in_iteration;
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
                check_binding_pattern_reserved(ctx, cn->param);
                walk_expression(ctx, cn->param);
                walk_statement(ctx, cn->body);
            }
            walk_statement(ctx, tn->finalizer);
            break;
        }

        case JS_AST_NODE_SWITCH_STATEMENT: {
            JsSwitchNode* sw = (JsSwitchNode*)node;
            walk_expression(ctx, sw->discriminant);
            // v17: all switch case clauses share one block scope for let/const
            // collect all let/const declarations across all cases for redeclaration check
            {
                struct hashmap* switch_scope = hashmap_new(sizeof(BlockScopeEntry), 16, 0, 0, bse_hash, bse_cmp, NULL, NULL);
                for (JsAstNode* c = sw->cases; c; c = c->next) {
                    if (c->node_type == JS_AST_NODE_SWITCH_CASE) {
                        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
                        for (JsAstNode* s = sc->consequent; s; s = s->next) {
                            if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                                JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)s;
                                if (vd->kind == JS_VAR_VAR) continue;
                                for (JsAstNode* decl = vd->declarations; decl; decl = decl->next) {
                                    if (decl->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                                    JsVariableDeclaratorNode* vdecl = (JsVariableDeclaratorNode*)decl;
                                    if (!vdecl->id || vdecl->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
                                    JsIdentifierNode* id = (JsIdentifierNode*)vdecl->id;
                                    if (!id->name) continue;
                                    const char* name = id->name->chars;
                                    BlockScopeEntry probe = {name, 0};
                                    const BlockScopeEntry* existing = (const BlockScopeEntry*)hashmap_get(switch_scope, &probe);
                                    if (existing) {
                                        ee_error(ctx, (JsAstNode*)id,
                                            "Identifier '%s' has already been declared in this scope", name);
                                    } else {
                                        BlockScopeEntry entry = {name, vd->kind};
                                        hashmap_set(switch_scope, &entry);
                                    }
                                }
                            }
                            if (s->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                                JsClassNode* cls = (JsClassNode*)s;
                                if (cls->name) {
                                    const char* name = cls->name->chars;
                                    BlockScopeEntry probe = {name, 0};
                                    const BlockScopeEntry* existing = (const BlockScopeEntry*)hashmap_get(switch_scope, &probe);
                                    if (existing) {
                                        ee_error(ctx, s, "Identifier '%s' has already been declared in this scope", name);
                                    } else {
                                        BlockScopeEntry entry = {name, JS_VAR_CONST};
                                        hashmap_set(switch_scope, &entry);
                                    }
                                }
                            }
                        }
                    }
                }
                hashmap_free(switch_scope);
            }
            bool saved_in_switch = ctx->in_switch;
            ctx->in_switch = true;
            for (JsAstNode* c = sw->cases; c; c = c->next) {
                if (c->node_type == JS_AST_NODE_SWITCH_CASE) {
                    JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
                    walk_expression(ctx, sc->test);
                    walk_statements(ctx, sc->consequent);
                }
            }
            ctx->in_switch = saved_in_switch;
            break;
        }

        case JS_AST_NODE_FUNCTION_DECLARATION: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            bool was_gen = ctx->in_generator;
            bool was_async = ctx->in_async;
            bool was_strict = ctx->in_strict;
            bool was_iteration = ctx->in_iteration;
            bool was_switch = ctx->in_switch;
            bool was_params = ctx->in_formal_parameters;
            int was_label_count = ctx->iteration_label_count;
            int was_all_label_count = ctx->all_label_count;
            ctx->in_generator = fn->is_generator;
            ctx->in_async = fn->is_async;
            ctx->in_iteration = false;
            ctx->in_switch = false;
            ctx->iteration_label_count = 0;
            ctx->all_label_count = 0;

            // v17: "use strict" with non-simple params is SyntaxError
            check_strict_non_simple(ctx, fn);

            if (fn->has_use_strict_directive) {
                ctx->in_strict = true;
            }

            check_duplicate_params(ctx, fn);
            check_function_name_reserved(ctx, fn);

            ctx->in_formal_parameters = true;
            for (JsAstNode* p = fn->params; p; p = p->next) {
                check_binding_pattern_reserved(ctx, p);
                walk_expression(ctx, p);
            }
            ctx->in_formal_parameters = was_params;
            walk_statement(ctx, fn->body);

            ctx->in_generator = was_gen;
            ctx->in_async = was_async;
            ctx->in_strict = was_strict;
            ctx->in_iteration = was_iteration;
            ctx->in_switch = was_switch;
            ctx->in_formal_parameters = was_params;
            ctx->iteration_label_count = was_label_count;
            ctx->all_label_count = was_all_label_count;
            break;
        }

        case JS_AST_NODE_CLASS_DECLARATION: {
            JsClassNode* cls = (JsClassNode*)node;
            bool was_strict = ctx->in_strict;
            int saved_private_count = ctx->private_name_count;
            ctx->in_strict = true; // class body is always strict
            collect_class_private_names(ctx, cls);
            walk_expression(ctx, cls->superclass);
            for (JsAstNode* m = cls->body; m; m = m->next) {
                walk_statement(ctx, m);
            }
            ctx->private_name_count = saved_private_count;
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

        case JS_AST_NODE_WITH_STATEMENT: {
            // v17: 'with' statements are forbidden in strict mode
            if (ctx->in_strict) {
                ee_error(ctx, node, "Strict mode code may not include a with statement");
            }
            break;
        }

        case JS_AST_NODE_LABELED_STATEMENT: {
            JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
            // v17: labeled class/lexical declarations are SyntaxError
            if (ls->body) {
                if (ls->body->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                    ee_error(ctx, node, "Class declaration cannot be labelled");
                }
                if (ls->body->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)ls->body;
                    if (vd->kind == JS_VAR_LET || vd->kind == JS_VAR_CONST) {
                        ee_error(ctx, node, "Lexical declaration cannot be labelled");
                    }
                }
                // Track whether this label is on an iteration statement
                // (for continue target validation per ContainsUndefinedContinueTarget)
                bool is_iteration = ls->body->node_type == JS_AST_NODE_FOR_STATEMENT ||
                                    ls->body->node_type == JS_AST_NODE_FOR_IN_STATEMENT ||
                                    ls->body->node_type == JS_AST_NODE_FOR_OF_STATEMENT ||
                                    ls->body->node_type == JS_AST_NODE_WHILE_STATEMENT ||
                                    ls->body->node_type == JS_AST_NODE_DO_WHILE_STATEMENT;
                int saved_count = ctx->iteration_label_count;
                if (is_iteration && ls->label && ctx->iteration_label_count < 32) {
                    ctx->iteration_labels[ctx->iteration_label_count] = ls->label;
                    ctx->iteration_label_lens[ctx->iteration_label_count] = ls->label_len;
                    ctx->iteration_label_count++;
                }
                // track every label in scope so labeled breaks can be validated
                int saved_all_count = ctx->all_label_count;
                if (ls->label && ctx->all_label_count < 64) {
                    ctx->all_labels[ctx->all_label_count] = ls->label;
                    ctx->all_label_lens[ctx->all_label_count] = ls->label_len;
                    ctx->all_label_count++;
                }
                walk_statement(ctx, ls->body);
                ctx->iteration_label_count = saved_count;
                ctx->all_label_count = saved_all_count;
            }
            break;
        }

        case JS_AST_NODE_CONTINUE_STATEMENT: {
            // continue without label: must be inside an iteration statement
            JsBreakContinueNode* cn = (JsBreakContinueNode*)node;
            if (!cn->label) {
                if (!ctx->in_iteration) {
                    ee_error(ctx, node, "Illegal continue statement: no surrounding iteration statement");
                }
            } else {
                // continue with label: label must refer to an iteration statement
                bool found = false;
                for (int i = 0; i < ctx->iteration_label_count; i++) {
                    if (ctx->iteration_label_lens[i] == cn->label_len &&
                        strncmp(ctx->iteration_labels[i], cn->label, cn->label_len) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ee_error(ctx, node, "Illegal continue statement: '%.*s' does not denote an iteration statement",
                        cn->label_len, cn->label);
                }
            }
            break;
        }

        case JS_AST_NODE_BREAK_STATEMENT: {
            // break without label: must be inside iteration or switch
            JsBreakContinueNode* bn = (JsBreakContinueNode*)node;
            if (!bn->label) {
                if (!ctx->in_iteration && !ctx->in_switch) {
                    ee_error(ctx, node, "Illegal break statement");
                }
            } else {
                // labeled break: the label must denote an enclosing labeled
                // statement within the same function/script/eval code
                // (ContainsUndefinedBreakTarget). Labels do not cross function
                // or eval boundaries.
                bool found = false;
                for (int i = 0; i < ctx->all_label_count; i++) {
                    if (ctx->all_label_lens[i] == bn->label_len &&
                        strncmp(ctx->all_labels[i], bn->label, bn->label_len) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ee_error(ctx, node, "Undefined break target: '%.*s'",
                        bn->label_len, bn->label);
                }
            }
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
        if (prog->has_use_strict_directive) {
            ctx.in_strict = true;
        }
        check_block_redeclarations(&ctx, prog->body);
        walk_statements(&ctx, prog->body);
    } else {
        walk_statement(&ctx, ast);
    }

    return ctx.error_count;
}
