// build_rb_ast.cpp — Build Ruby AST from Tree-sitter CST
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include <cstring>
#include "../../lib/mem.h"
#include <cstdio>
#include <cstdint>
#include <cerrno>

// source text extraction macro
#define rb_node_source(transpiler, node) {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

// forward declarations
static RbAstNode* build_rb_body_statements(RbTranspiler* tp, TSNode body_node);
static RbAstNode* build_rb_if_statement(RbTranspiler* tp, TSNode if_node, bool is_unless);
static RbAstNode* build_rb_while_statement(RbTranspiler* tp, TSNode wh_node, bool is_until);
static RbAstNode* build_rb_for_statement(RbTranspiler* tp, TSNode for_node);
static RbAstNode* build_rb_case_statement(RbTranspiler* tp, TSNode case_node);
static RbAstNode* build_rb_class_def(RbTranspiler* tp, TSNode class_node);
static RbAstNode* build_rb_module_def(RbTranspiler* tp, TSNode mod_node);
static RbAstNode* build_rb_begin_rescue(RbTranspiler* tp, TSNode begin_node);
static RbAstNode* build_rb_string(RbTranspiler* tp, TSNode string_node);
static RbAstNode* build_rb_method_params(RbTranspiler* tp, TSNode params_node);
static RbAstNode* build_rb_block_node(RbTranspiler* tp, TSNode block_node);

// ============================================================================
// Allocation and operator helpers
// ============================================================================

RbAstNode* alloc_rb_ast_node(RbTranspiler* tp, RbAstNodeType node_type, TSNode node, size_t size) {
    RbAstNode* ast_node = (RbAstNode*)arena_calloc(tp->ast_arena, size);
    ast_node->node_type = node_type;
    ast_node->node = node;
    return ast_node;
}

RbOperator rb_operator_from_string(const char* op_str, size_t len) {
    if (len == 1) {
        switch (op_str[0]) {
            case '+': return RB_OP_ADD;
            case '-': return RB_OP_SUB;
            case '*': return RB_OP_MUL;
            case '/': return RB_OP_DIV;
            case '%': return RB_OP_MOD;
            case '<': return RB_OP_LT;
            case '>': return RB_OP_GT;
            case '~': return RB_OP_BIT_NOT;
            case '&': return RB_OP_BIT_AND;
            case '|': return RB_OP_BIT_OR;
            case '^': return RB_OP_BIT_XOR;
            case '!': return RB_OP_NOT;
        }
    } else if (len == 2) {
        if (strncmp(op_str, "==", 2) == 0) return RB_OP_EQ;
        if (strncmp(op_str, "!=", 2) == 0) return RB_OP_NEQ;
        if (strncmp(op_str, "<=", 2) == 0) return RB_OP_LE;
        if (strncmp(op_str, ">=", 2) == 0) return RB_OP_GE;
        if (strncmp(op_str, "**", 2) == 0) return RB_OP_POW;
        if (strncmp(op_str, "<<", 2) == 0) return RB_OP_LSHIFT;
        if (strncmp(op_str, ">>", 2) == 0) return RB_OP_RSHIFT;
        if (strncmp(op_str, "&&", 2) == 0) return RB_OP_AND;
        if (strncmp(op_str, "||", 2) == 0) return RB_OP_OR;
        if (strncmp(op_str, "or", 2) == 0) return RB_OP_OR;
        if (strncmp(op_str, "=~", 2) == 0) return RB_OP_MATCH;
        if (strncmp(op_str, "!~", 2) == 0) return RB_OP_NOT_MATCH;
    } else if (len == 3) {
        if (strncmp(op_str, "<=>", 3) == 0) return RB_OP_CMP;
        if (strncmp(op_str, "===", 3) == 0) return RB_OP_CASE_EQ;
        if (strncmp(op_str, "and", 3) == 0) return RB_OP_AND;
        if (strncmp(op_str, "not", 3) == 0) return RB_OP_NOT;
    }

    log_error("rb: unknown operator: %.*s", (int)len, op_str);
    return RB_OP_ADD;
}

// Map augmented assignment operator text to base operator
static RbOperator rb_aug_operator(const char* op_str, size_t len) {
    if (len == 2) {
        if (strncmp(op_str, "+=", 2) == 0) return RB_OP_ADD;
        if (strncmp(op_str, "-=", 2) == 0) return RB_OP_SUB;
        if (strncmp(op_str, "*=", 2) == 0) return RB_OP_MUL;
        if (strncmp(op_str, "/=", 2) == 0) return RB_OP_DIV;
        if (strncmp(op_str, "%=", 2) == 0) return RB_OP_MOD;
        if (strncmp(op_str, "&=", 2) == 0) return RB_OP_BIT_AND;
        if (strncmp(op_str, "|=", 2) == 0) return RB_OP_BIT_OR;
        if (strncmp(op_str, "^=", 2) == 0) return RB_OP_BIT_XOR;
    } else if (len == 3) {
        if (strncmp(op_str, "**=", 3) == 0) return RB_OP_POW;
        if (strncmp(op_str, "<<=", 3) == 0) return RB_OP_LSHIFT;
        if (strncmp(op_str, ">>=", 3) == 0) return RB_OP_RSHIFT;
        if (strncmp(op_str, "&&=", 3) == 0) return RB_OP_AND;
        if (strncmp(op_str, "||=", 3) == 0) return RB_OP_OR;
    }

    log_error("rb: unknown augmented operator: %.*s", (int)len, op_str);
    return RB_OP_ADD;
}

// ============================================================================
// Literal builders
// ============================================================================

static RbAstNode* build_rb_integer(RbTranspiler* tp, TSNode int_node) {
    RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(tp, RB_AST_NODE_LITERAL, int_node, sizeof(RbLiteralNode));
    lit->literal_type = RB_LITERAL_INT;

    StrView source = rb_node_source(tp, int_node);
    // Ruby allows underscores in numbers: 1_000_000
    char temp[128];
    size_t j = 0;
    for (size_t i = 0; i < source.length && j < sizeof(temp) - 1; i++) {
        if (source.str[i] != '_') temp[j++] = source.str[i];
    }
    temp[j] = '\0';

    errno = 0;
    if (j > 2 && temp[0] == '0') {
        char prefix = temp[1];
        if (prefix == 'x' || prefix == 'X') {
            lit->value.int_value = strtoll(temp, NULL, 16);
        } else if (prefix == 'o' || prefix == 'O') {
            lit->value.int_value = strtoll(temp, NULL, 8);
        } else if (prefix == 'b' || prefix == 'B') {
            lit->value.int_value = strtoll(temp, NULL, 2);
        } else {
            lit->value.int_value = strtoll(temp, NULL, 10);
        }
    } else {
        lit->value.int_value = strtoll(temp, NULL, 10);
    }

    lit->base.type = &TYPE_INT;
    return (RbAstNode*)lit;
}

static RbAstNode* build_rb_float(RbTranspiler* tp, TSNode float_node) {
    RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(tp, RB_AST_NODE_LITERAL, float_node, sizeof(RbLiteralNode));
    lit->literal_type = RB_LITERAL_FLOAT;

    StrView source = rb_node_source(tp, float_node);
    char temp[128];
    size_t j = 0;
    for (size_t i = 0; i < source.length && j < sizeof(temp) - 1; i++) {
        if (source.str[i] != '_') temp[j++] = source.str[i];
    }
    temp[j] = '\0';
    lit->value.float_value = strtod(temp, NULL);

    lit->base.type = &TYPE_FLOAT;
    return (RbAstNode*)lit;
}

static RbAstNode* build_rb_boolean(RbTranspiler* tp, TSNode bool_node, bool value) {
    RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(tp, RB_AST_NODE_LITERAL, bool_node, sizeof(RbLiteralNode));
    lit->literal_type = RB_LITERAL_BOOLEAN;
    lit->value.boolean_value = value;
    lit->base.type = &TYPE_BOOL;
    return (RbAstNode*)lit;
}

static RbAstNode* build_rb_nil(RbTranspiler* tp, TSNode nil_node) {
    RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(tp, RB_AST_NODE_LITERAL, nil_node, sizeof(RbLiteralNode));
    lit->literal_type = RB_LITERAL_NIL;
    lit->base.type = &TYPE_NULL;
    return (RbAstNode*)lit;
}

// ============================================================================
// Identifier and variable builders
// ============================================================================

static RbAstNode* build_rb_identifier(RbTranspiler* tp, TSNode id_node) {
    if (ts_node_is_null(id_node)) return NULL;

    RbIdentifierNode* id = (RbIdentifierNode*)alloc_rb_ast_node(tp, RB_AST_NODE_IDENTIFIER, id_node, sizeof(RbIdentifierNode));

    StrView source = rb_node_source(tp, id_node);
    id->name = name_pool_create_len(tp->name_pool, source.str, source.length);
    id->entry = rb_scope_lookup(tp, id->name);
    id->base.type = id->entry ? id->entry->node->type : &TYPE_ANY;

    return (RbAstNode*)id;
}

static RbAstNode* build_rb_instance_variable(RbTranspiler* tp, TSNode node) {
    RbIvarNode* iv = (RbIvarNode*)alloc_rb_ast_node(tp, RB_AST_NODE_IVAR, node, sizeof(RbIvarNode));

    StrView source = rb_node_source(tp, node);
    // skip @ prefix
    if (source.length > 1 && source.str[0] == '@') {
        iv->name = name_pool_create_len(tp->name_pool, source.str + 1, source.length - 1);
    } else {
        iv->name = name_pool_create_len(tp->name_pool, source.str, source.length);
    }

    iv->base.type = &TYPE_ANY;
    return (RbAstNode*)iv;
}

static RbAstNode* build_rb_class_variable(RbTranspiler* tp, TSNode node) {
    RbCvarNode* cv = (RbCvarNode*)alloc_rb_ast_node(tp, RB_AST_NODE_CVAR, node, sizeof(RbCvarNode));

    StrView source = rb_node_source(tp, node);
    // skip @@ prefix
    if (source.length > 2 && source.str[0] == '@' && source.str[1] == '@') {
        cv->name = name_pool_create_len(tp->name_pool, source.str + 2, source.length - 2);
    } else {
        cv->name = name_pool_create_len(tp->name_pool, source.str, source.length);
    }

    cv->base.type = &TYPE_ANY;
    return (RbAstNode*)cv;
}

static RbAstNode* build_rb_global_variable(RbTranspiler* tp, TSNode node) {
    RbGvarNode* gv = (RbGvarNode*)alloc_rb_ast_node(tp, RB_AST_NODE_GVAR, node, sizeof(RbGvarNode));

    StrView source = rb_node_source(tp, node);
    // skip $ prefix
    if (source.length > 1 && source.str[0] == '$') {
        gv->name = name_pool_create_len(tp->name_pool, source.str + 1, source.length - 1);
    } else {
        gv->name = name_pool_create_len(tp->name_pool, source.str, source.length);
    }

    gv->base.type = &TYPE_ANY;
    return (RbAstNode*)gv;
}

static RbAstNode* build_rb_constant(RbTranspiler* tp, TSNode node) {
    RbConstNode* cn = (RbConstNode*)alloc_rb_ast_node(tp, RB_AST_NODE_CONST, node, sizeof(RbConstNode));

    StrView source = rb_node_source(tp, node);
    cn->name = name_pool_create_len(tp->name_pool, source.str, source.length);

    cn->base.type = &TYPE_ANY;
    return (RbAstNode*)cn;
}

// ============================================================================
// Symbol builder
// ============================================================================

static RbAstNode* build_rb_symbol(RbTranspiler* tp, TSNode sym_node) {
    RbSymbolNode* sym = (RbSymbolNode*)alloc_rb_ast_node(tp, RB_AST_NODE_SYMBOL, sym_node, sizeof(RbSymbolNode));

    StrView source = rb_node_source(tp, sym_node);
    // simple_symbol: :name — skip the colon
    if (source.length > 1 && source.str[0] == ':') {
        sym->name = name_pool_create_len(tp->name_pool, source.str + 1, source.length - 1);
    } else {
        // delimited_symbol or hash_key_symbol — extract content
        uint32_t child_count = ts_node_named_child_count(sym_node);
        if (child_count > 0) {
            TSNode content = ts_node_named_child(sym_node, 0);
            StrView cv = rb_node_source(tp, content);
            sym->name = name_pool_create_len(tp->name_pool, cv.str, cv.length);
        } else {
            sym->name = name_pool_create_len(tp->name_pool, source.str, source.length);
        }
    }

    sym->base.type = &TYPE_ANY;
    return (RbAstNode*)sym;
}

// ============================================================================
// String builder
// ============================================================================

static int rb_decode_escape(const char* src, size_t src_len, char* out) {
    if (src_len < 2 || src[0] != '\\') return 0;

    switch (src[1]) {
        case 'n':  *out = '\n'; return 2;
        case 't':  *out = '\t'; return 2;
        case 'r':  *out = '\r'; return 2;
        case '\\': *out = '\\'; return 2;
        case '\'': *out = '\''; return 2;
        case '"':  *out = '"';  return 2;
        case '0':  *out = '\0'; return 2;
        case 'a':  *out = '\a'; return 2;
        case 'b':  *out = '\b'; return 2;
        case 'f':  *out = '\f'; return 2;
        case 'v':  *out = '\v'; return 2;
        case 's':  *out = ' ';  return 2;
        default:   *out = src[1]; return 2;
    }
}

static RbAstNode* build_rb_string(RbTranspiler* tp, TSNode string_node) {
    // tree-sitter-ruby string has children: string_content, escape_sequence, interpolation
    const char* stype = ts_node_type(string_node);

    // heredoc_body: tree-sitter-ruby gives us heredoc_content / interpolation children
    if (strcmp(stype, "heredoc_body") == 0) {
        // first pass: check for interpolation
        bool hd_has_interp = false;
        uint32_t cc = ts_node_child_count(string_node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(string_node, i);
            if (strcmp(ts_node_type(child), "interpolation") == 0) {
                hd_has_interp = true;
                break;
            }
        }

        if (hd_has_interp) {
            // build string interpolation node from heredoc parts
            RbStringInterpNode* interp = (RbStringInterpNode*)alloc_rb_ast_node(
                tp, RB_AST_NODE_STRING_INTERPOLATION, string_node, sizeof(RbStringInterpNode));
            interp->part_count = 0;
            RbAstNode* prev = NULL;
            bool first_content = true;
            for (uint32_t i = 0; i < cc; i++) {
                TSNode child = ts_node_child(string_node, i);
                const char* ct = ts_node_type(child);
                RbAstNode* part = NULL;
                if (strcmp(ct, "heredoc_content") == 0 || strcmp(ct, "string_content") == 0) {
                    StrView content = rb_node_source(tp, child);
                    // strip leading newline from first heredoc content
                    if (first_content && content.length > 0 && content.str[0] == '\n') {
                        content.str++;
                        content.length--;
                    }
                    first_content = false;
                    if (content.length > 0) {
                        RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
                            tp, RB_AST_NODE_LITERAL, child, sizeof(RbLiteralNode));
                        lit->literal_type = RB_LITERAL_STRING;
                        lit->value.string_value = name_pool_create_len(tp->name_pool, content.str, content.length);
                        lit->base.type = &TYPE_STRING;
                        part = (RbAstNode*)lit;
                    }
                } else if (strcmp(ct, "interpolation") == 0) {
                    uint32_t ic = ts_node_named_child_count(child);
                    if (ic > 0) {
                        part = build_rb_expression(tp, ts_node_named_child(child, 0));
                    }
                } else if (strcmp(ct, "escape_sequence") == 0) {
                    StrView esc_src = rb_node_source(tp, child);
                    char decoded;
                    if (rb_decode_escape(esc_src.str, esc_src.length, &decoded) > 0) {
                        RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
                            tp, RB_AST_NODE_LITERAL, child, sizeof(RbLiteralNode));
                        lit->literal_type = RB_LITERAL_STRING;
                        lit->value.string_value = name_pool_create_len(tp->name_pool, &decoded, 1);
                        lit->base.type = &TYPE_STRING;
                        part = (RbAstNode*)lit;
                    }
                }
                if (part) {
                    interp->part_count++;
                    if (!prev) { interp->parts = part; } else { prev->next = part; }
                    prev = part;
                }
            }
            interp->base.type = &TYPE_STRING;
            return (RbAstNode*)interp;
        }

        // plain heredoc (no interpolation) — collect content
        char buf[4096];
        size_t buf_len = 0;
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(string_node, i);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "heredoc_content") == 0 || strcmp(ct, "string_content") == 0) {
                StrView content = rb_node_source(tp, child);
                size_t copy_len = content.length;
                if (buf_len + copy_len >= sizeof(buf)) copy_len = sizeof(buf) - buf_len - 1;
                memcpy(buf + buf_len, content.str, copy_len);
                buf_len += copy_len;
            } else if (strcmp(ct, "escape_sequence") == 0) {
                StrView esc_src = rb_node_source(tp, child);
                char decoded;
                if (rb_decode_escape(esc_src.str, esc_src.length, &decoded) > 0) {
                    if (buf_len < sizeof(buf) - 1) buf[buf_len++] = decoded;
                }
            }
        }
        // strip leading newline (tree-sitter includes newline after heredoc tag)
        if (buf_len > 0 && buf[0] == '\n') {
            memmove(buf, buf + 1, buf_len - 1);
            buf_len--;
        }
        // strip common leading whitespace for <<~ (squiggly heredoc)
        {
            int min_indent = 9999;
            const char* p = buf;
            const char* end = buf + buf_len;
            while (p < end) {
                const char* line_start = p;
                while (p < end && *p != '\n') p++;
                int line_len = (int)(p - line_start);
                if (line_len > 0) {
                    int indent = 0;
                    while (indent < line_len && line_start[indent] == ' ') indent++;
                    if (indent < line_len) {
                        if (indent < min_indent) min_indent = indent;
                    }
                }
                if (p < end) p++;
            }
            if (min_indent > 0 && min_indent < 9999) {
                char buf2[4096];
                size_t buf2_len = 0;
                p = buf;
                while (p < end) {
                    const char* line_start = p;
                    while (p < end && *p != '\n') p++;
                    int line_len = (int)(p - line_start);
                    int skip = (line_len > 0 && min_indent <= line_len) ? min_indent : 0;
                    size_t copy_len = line_len - skip;
                    if (buf2_len + copy_len + 1 < sizeof(buf2)) {
                        memcpy(buf2 + buf2_len, line_start + skip, copy_len);
                        buf2_len += copy_len;
                        if (p < end) buf2[buf2_len++] = '\n';
                    }
                    if (p < end) p++;
                }
                memcpy(buf, buf2, buf2_len);
                buf_len = buf2_len;
            }
        }
        buf[buf_len] = '\0';
        RbLiteralNode* literal = (RbLiteralNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_LITERAL, string_node, sizeof(RbLiteralNode));
        literal->literal_type = RB_LITERAL_STRING;
        literal->value.string_value = name_pool_create_len(tp->name_pool, buf, buf_len);
        literal->base.type = &TYPE_STRING;
        return (RbAstNode*)literal;
    }

    uint32_t child_count = ts_node_named_child_count(string_node);

    // check for interpolations
    bool has_interpolation = false;
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(string_node, i);
        if (strcmp(ts_node_type(child), "interpolation") == 0) {
            has_interpolation = true;
            break;
        }
    }

    if (has_interpolation) {
        // build string interpolation node
        RbStringInterpNode* interp = (RbStringInterpNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_STRING_INTERPOLATION, string_node, sizeof(RbStringInterpNode));
        interp->part_count = 0;

        RbAstNode* prev = NULL;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(string_node, i);
            const char* child_type = ts_node_type(child);
            RbAstNode* part = NULL;

            if (strcmp(child_type, "string_content") == 0) {
                StrView content = rb_node_source(tp, child);
                RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
                    tp, RB_AST_NODE_LITERAL, child, sizeof(RbLiteralNode));
                lit->literal_type = RB_LITERAL_STRING;
                lit->value.string_value = name_pool_create_len(tp->name_pool, content.str, content.length);
                lit->base.type = &TYPE_STRING;
                part = (RbAstNode*)lit;
            } else if (strcmp(child_type, "interpolation") == 0) {
                // interpolation children: _statement
                uint32_t interp_children = ts_node_named_child_count(child);
                if (interp_children > 0) {
                    TSNode expr_child = ts_node_named_child(child, 0);
                    part = build_rb_expression(tp, expr_child);
                }
            } else if (strcmp(child_type, "escape_sequence") == 0) {
                StrView esc_src = rb_node_source(tp, child);
                char decoded;
                if (rb_decode_escape(esc_src.str, esc_src.length, &decoded) > 0) {
                    RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
                        tp, RB_AST_NODE_LITERAL, child, sizeof(RbLiteralNode));
                    lit->literal_type = RB_LITERAL_STRING;
                    lit->value.string_value = name_pool_create_len(tp->name_pool, &decoded, 1);
                    lit->base.type = &TYPE_STRING;
                    part = (RbAstNode*)lit;
                }
            }

            if (part) {
                interp->part_count++;
                if (!prev) {
                    interp->parts = part;
                } else {
                    prev->next = part;
                }
                prev = part;
            }
        }

        interp->base.type = &TYPE_STRING;
        return (RbAstNode*)interp;
    }

    // plain string — collect content
    RbLiteralNode* literal = (RbLiteralNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_LITERAL, string_node, sizeof(RbLiteralNode));
    literal->literal_type = RB_LITERAL_STRING;

    char buf[4096];
    size_t buf_len = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(string_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "string_content") == 0) {
            StrView content = rb_node_source(tp, child);
            uint32_t inner = ts_node_named_child_count(child);
            if (inner == 0) {
                size_t copy_len = content.length;
                if (buf_len + copy_len >= sizeof(buf)) copy_len = sizeof(buf) - buf_len - 1;
                memcpy(buf + buf_len, content.str, copy_len);
                buf_len += copy_len;
            } else {
                // has escape sequences inside
                uint32_t pos = ts_node_start_byte(child);
                uint32_t end = ts_node_end_byte(child);
                uint32_t esc_idx = 0;

                while (pos < end && buf_len < sizeof(buf) - 1) {
                    bool is_escape = false;
                    if (esc_idx < inner) {
                        TSNode esc_child = ts_node_named_child(child, esc_idx);
                        if (strcmp(ts_node_type(esc_child), "escape_sequence") == 0) {
                            uint32_t esc_start = ts_node_start_byte(esc_child);
                            uint32_t esc_end = ts_node_end_byte(esc_child);

                            if (pos < esc_start) {
                                size_t pre_len = esc_start - pos;
                                if (buf_len + pre_len >= sizeof(buf)) pre_len = sizeof(buf) - buf_len - 1;
                                memcpy(buf + buf_len, tp->source + pos, pre_len);
                                buf_len += pre_len;
                            }

                            const char* esc_src = tp->source + esc_start;
                            size_t esc_len = esc_end - esc_start;
                            char decoded;
                            if (rb_decode_escape(esc_src, esc_len, &decoded) > 0) {
                                buf[buf_len++] = decoded;
                            }

                            pos = esc_end;
                            esc_idx++;
                            is_escape = true;
                        }
                    }
                    if (!is_escape) {
                        buf[buf_len++] = tp->source[pos++];
                    }
                }
            }
        } else if (strcmp(child_type, "escape_sequence") == 0) {
            StrView esc_src = rb_node_source(tp, child);
            char decoded;
            if (rb_decode_escape(esc_src.str, esc_src.length, &decoded) > 0) {
                if (buf_len < sizeof(buf) - 1) buf[buf_len++] = decoded;
            }
        }
    }

    // fallback for empty/simple strings
    if (child_count == 0) {
        StrView source = rb_node_source(tp, string_node);
        const char* start = source.str;
        size_t len = source.length;
        // strip outer quotes
        if (len >= 2) {
            start++; len -= 2;
        }
        size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, start, copy_len);
        buf_len = copy_len;
    }

    buf[buf_len] = '\0';
    literal->value.string_value = name_pool_create_len(tp->name_pool, buf, buf_len);
    literal->base.type = &TYPE_STRING;
    return (RbAstNode*)literal;
}

// ============================================================================
// Expression builders
// ============================================================================

// Build binary operation
static RbAstNode* build_rb_binary_op(RbTranspiler* tp, TSNode binary_node) {
    TSNode left_node = ts_node_child_by_field_name(binary_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(binary_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(binary_node, "operator", 8);

    StrView op_source = {0};
    if (!ts_node_is_null(op_node)) {
        op_source = rb_node_source(tp, op_node);
    }

    // boolean operators → RbBooleanNode
    if (op_source.length > 0) {
        if ((op_source.length == 2 && (strncmp(op_source.str, "&&", 2) == 0 || strncmp(op_source.str, "||", 2) == 0)) ||
            (op_source.length == 2 && strncmp(op_source.str, "or", 2) == 0) ||
            (op_source.length == 3 && strncmp(op_source.str, "and", 3) == 0)) {
            RbBooleanNode* bop = (RbBooleanNode*)alloc_rb_ast_node(tp, RB_AST_NODE_BOOLEAN_OP, binary_node, sizeof(RbBooleanNode));
            bop->op = rb_operator_from_string(op_source.str, op_source.length);
            bop->left = build_rb_expression(tp, left_node);
            bop->right = build_rb_expression(tp, right_node);
            bop->base.type = &TYPE_ANY;
            return (RbAstNode*)bop;
        }

        // comparison operators → RbBinaryNode with COMPARISON type
        if ((op_source.length == 2 && (strncmp(op_source.str, "==", 2) == 0 || strncmp(op_source.str, "!=", 2) == 0 ||
             strncmp(op_source.str, "<=", 2) == 0 || strncmp(op_source.str, ">=", 2) == 0 ||
             strncmp(op_source.str, "=~", 2) == 0 || strncmp(op_source.str, "!~", 2) == 0)) ||
            (op_source.length == 1 && (op_source.str[0] == '<' || op_source.str[0] == '>')) ||
            (op_source.length == 3 && (strncmp(op_source.str, "<=>", 3) == 0 || strncmp(op_source.str, "===", 3) == 0))) {
            RbBinaryNode* cmp = (RbBinaryNode*)alloc_rb_ast_node(tp, RB_AST_NODE_COMPARISON, binary_node, sizeof(RbBinaryNode));
            cmp->op = rb_operator_from_string(op_source.str, op_source.length);
            cmp->left = build_rb_expression(tp, left_node);
            cmp->right = build_rb_expression(tp, right_node);
            cmp->base.type = &TYPE_BOOL;
            return (RbAstNode*)cmp;
        }
    }

    // arithmetic / bitwise operators
    RbBinaryNode* binary = (RbBinaryNode*)alloc_rb_ast_node(tp, RB_AST_NODE_BINARY_OP, binary_node, sizeof(RbBinaryNode));
    binary->left = build_rb_expression(tp, left_node);
    binary->right = build_rb_expression(tp, right_node);

    if (op_source.length > 0) {
        binary->op = rb_operator_from_string(op_source.str, op_source.length);
    }

    binary->base.type = &TYPE_ANY;
    return (RbAstNode*)binary;
}

// Build unary operation
static RbAstNode* build_rb_unary_op(RbTranspiler* tp, TSNode unary_node) {
    TSNode op_node = ts_node_child_by_field_name(unary_node, "operator", 8);
    TSNode operand_node = ts_node_child_by_field_name(unary_node, "operand", 7);

    StrView op_source = {0};
    if (!ts_node_is_null(op_node)) {
        op_source = rb_node_source(tp, op_node);
    }

    // defined? — returns string describing operand type
    if (op_source.length >= 7 && memcmp(op_source.str, "defined", 7) == 0) {
        RbDefinedNode* def = (RbDefinedNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_DEFINED, unary_node, sizeof(RbDefinedNode));
        if (!ts_node_is_null(operand_node)) {
            def->operand = build_rb_expression(tp, operand_node);
        }
        def->base.type = &TYPE_STRING;
        return (RbAstNode*)def;
    }

    // 'not' and '!' — boolean unary
    if ((op_source.length == 1 && op_source.str[0] == '!') ||
        (op_source.length == 3 && strncmp(op_source.str, "not", 3) == 0)) {
        RbBooleanNode* bn = (RbBooleanNode*)alloc_rb_ast_node(tp, RB_AST_NODE_BOOLEAN_OP, unary_node, sizeof(RbBooleanNode));
        bn->op = RB_OP_NOT;
        bn->left = build_rb_expression(tp, operand_node);
        bn->right = NULL;
        bn->base.type = &TYPE_BOOL;
        return (RbAstNode*)bn;
    }

    RbUnaryNode* unary = (RbUnaryNode*)alloc_rb_ast_node(tp, RB_AST_NODE_UNARY_OP, unary_node, sizeof(RbUnaryNode));
    unary->operand = build_rb_expression(tp, operand_node);

    if (op_source.length == 1 && op_source.str[0] == '-') {
        unary->op = RB_OP_NEGATE;
    } else if (op_source.length == 1 && op_source.str[0] == '+') {
        unary->op = RB_OP_POSITIVE;
    } else if (op_source.length == 1 && op_source.str[0] == '~') {
        unary->op = RB_OP_BIT_NOT;
    }

    unary->base.type = &TYPE_ANY;
    return (RbAstNode*)unary;
}

// Build method call
static RbAstNode* build_rb_call(RbTranspiler* tp, TSNode call_node) {
    RbCallNode* call = (RbCallNode*)alloc_rb_ast_node(tp, RB_AST_NODE_CALL, call_node, sizeof(RbCallNode));

    // tree-sitter-ruby call has fields: receiver, method, operator, arguments, block
    TSNode recv_node = ts_node_child_by_field_name(call_node, "receiver", 8);
    TSNode method_node = ts_node_child_by_field_name(call_node, "method", 6);
    TSNode args_node = ts_node_child_by_field_name(call_node, "arguments", 9);
    TSNode block_node = ts_node_child_by_field_name(call_node, "block", 5);

    if (!ts_node_is_null(recv_node)) {
        call->receiver = build_rb_expression(tp, recv_node);
    }

    if (!ts_node_is_null(method_node)) {
        StrView method_src = rb_node_source(tp, method_node);
        call->method_name = name_pool_create_len(tp->name_pool, method_src.str, method_src.length);
    }

    // build argument list
    call->arg_count = 0;
    if (!ts_node_is_null(args_node)) {
        uint32_t arg_count = ts_node_named_child_count(args_node);
        RbAstNode* prev = NULL;

        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg = ts_node_named_child(args_node, i);
            const char* arg_type = ts_node_type(arg);
            RbAstNode* arg_node = NULL;

            if (strcmp(arg_type, "splat_argument") == 0) {
                RbSplatNode* sp = (RbSplatNode*)alloc_rb_ast_node(tp, RB_AST_NODE_SPLAT, arg, sizeof(RbSplatNode));
                if (ts_node_named_child_count(arg) > 0) {
                    sp->operand = build_rb_expression(tp, ts_node_named_child(arg, 0));
                }
                sp->base.type = &TYPE_ANY;
                arg_node = (RbAstNode*)sp;
                call->has_splat = true;
            } else if (strcmp(arg_type, "hash_splat_argument") == 0) {
                RbSplatNode* sp = (RbSplatNode*)alloc_rb_ast_node(tp, RB_AST_NODE_DOUBLE_SPLAT, arg, sizeof(RbSplatNode));
                if (ts_node_named_child_count(arg) > 0) {
                    sp->operand = build_rb_expression(tp, ts_node_named_child(arg, 0));
                }
                sp->base.type = &TYPE_ANY;
                arg_node = (RbAstNode*)sp;
            } else if (strcmp(arg_type, "block_argument") == 0) {
                RbBlockPassNode* bp = (RbBlockPassNode*)alloc_rb_ast_node(tp, RB_AST_NODE_BLOCK_PASS, arg, sizeof(RbBlockPassNode));
                if (ts_node_named_child_count(arg) > 0) {
                    bp->value = build_rb_expression(tp, ts_node_named_child(arg, 0));
                }
                bp->base.type = &TYPE_ANY;
                arg_node = (RbAstNode*)bp;
                call->has_block_pass = true;
            } else if (strcmp(arg_type, "pair") == 0) {
                // inline hash argument: method(key: value) — build as pair
                arg_node = build_rb_expression(tp, arg);
            } else {
                arg_node = build_rb_expression(tp, arg);
            }

            if (arg_node) {
                call->arg_count++;
                if (!prev) {
                    call->args = arg_node;
                } else {
                    prev->next = arg_node;
                }
                prev = arg_node;
            }
        }
    }

    // build associated block
    if (!ts_node_is_null(block_node)) {
        call->block = build_rb_block_node(tp, block_node);
    }

    call->base.type = &TYPE_ANY;
    return (RbAstNode*)call;
}

// Build element reference (obj[key]) 
static RbAstNode* build_rb_element_reference(RbTranspiler* tp, TSNode elem_node) {
    RbSubscriptNode* sub = (RbSubscriptNode*)alloc_rb_ast_node(tp, RB_AST_NODE_SUBSCRIPT, elem_node, sizeof(RbSubscriptNode));

    TSNode object_node = ts_node_child_by_field_name(elem_node, "object", 6);
    sub->object = build_rb_expression(tp, object_node);

    // arguments are unnamed children (expressions inside [])
    uint32_t child_count = ts_node_named_child_count(elem_node);
    // find first expression child that isn't the object
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(elem_node, i);
        uint32_t child_start = ts_node_start_byte(child);
        uint32_t obj_end = ts_node_end_byte(object_node);
        if (child_start >= obj_end) {
            sub->index = build_rb_expression(tp, child);
            break;
        }
    }

    sub->base.type = &TYPE_ANY;
    return (RbAstNode*)sub;
}

// Build array literal
static RbAstNode* build_rb_array(RbTranspiler* tp, TSNode array_node) {
    RbArrayNode* arr = (RbArrayNode*)alloc_rb_ast_node(tp, RB_AST_NODE_ARRAY, array_node, sizeof(RbArrayNode));
    arr->count = 0;

    uint32_t child_count = ts_node_named_child_count(array_node);
    RbAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(array_node, i);
        RbAstNode* elem = build_rb_expression(tp, child);
        if (elem) {
            arr->count++;
            if (!prev) {
                arr->elements = elem;
            } else {
                prev->next = elem;
            }
            prev = elem;
        }
    }

    arr->base.type = &TYPE_ANY;
    return (RbAstNode*)arr;
}

// Build hash literal
static RbAstNode* build_rb_hash(RbTranspiler* tp, TSNode hash_node) {
    RbHashNode* hash = (RbHashNode*)alloc_rb_ast_node(tp, RB_AST_NODE_HASH, hash_node, sizeof(RbHashNode));
    hash->count = 0;

    uint32_t child_count = ts_node_named_child_count(hash_node);
    RbAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(hash_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "pair") == 0) {
            RbPairNode* pair = (RbPairNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PAIR, child, sizeof(RbPairNode));

            TSNode key_node = ts_node_child_by_field_name(child, "key", 3);
            TSNode val_node = ts_node_child_by_field_name(child, "value", 5);

            if (!ts_node_is_null(key_node)) {
                const char* key_type = ts_node_type(key_node);
                if (strcmp(key_type, "hash_key_symbol") == 0) {
                    // key: value syntax — treat as symbol
                    pair->key = build_rb_symbol(tp, key_node);
                } else {
                    pair->key = build_rb_expression(tp, key_node);
                }
            }
            if (!ts_node_is_null(val_node)) {
                pair->value = build_rb_expression(tp, val_node);
            }

            pair->base.type = &TYPE_ANY;
            hash->count++;

            if (!prev) {
                hash->pairs = (RbAstNode*)pair;
            } else {
                prev->next = (RbAstNode*)pair;
            }
            prev = (RbAstNode*)pair;
        } else if (strcmp(child_type, "hash_splat_argument") == 0) {
            RbSplatNode* sp = (RbSplatNode*)alloc_rb_ast_node(tp, RB_AST_NODE_DOUBLE_SPLAT, child, sizeof(RbSplatNode));
            if (ts_node_named_child_count(child) > 0) {
                sp->operand = build_rb_expression(tp, ts_node_named_child(child, 0));
            }
            sp->base.type = &TYPE_ANY;
            hash->count++;

            if (!prev) {
                hash->pairs = (RbAstNode*)sp;
            } else {
                prev->next = (RbAstNode*)sp;
            }
            prev = (RbAstNode*)sp;
        }
    }

    hash->base.type = &TYPE_ANY;
    return (RbAstNode*)hash;
}

// Build range expression (a..b or a...b)
static RbAstNode* build_rb_range(RbTranspiler* tp, TSNode range_node) {
    RbRangeNode* rng = (RbRangeNode*)alloc_rb_ast_node(tp, RB_AST_NODE_RANGE, range_node, sizeof(RbRangeNode));

    TSNode begin_node = ts_node_child_by_field_name(range_node, "begin", 5);
    TSNode end_node = ts_node_child_by_field_name(range_node, "end", 3);
    TSNode op_node = ts_node_child_by_field_name(range_node, "operator", 8);

    if (!ts_node_is_null(begin_node)) {
        rng->start = build_rb_expression(tp, begin_node);
    }
    if (!ts_node_is_null(end_node)) {
        rng->end = build_rb_expression(tp, end_node);
    }

    // determine exclusive (...)
    if (!ts_node_is_null(op_node)) {
        StrView op_src = rb_node_source(tp, op_node);
        rng->exclusive = (op_src.length == 3 && strncmp(op_src.str, "...", 3) == 0);
    }

    rng->base.type = &TYPE_ANY;
    return (RbAstNode*)rng;
}

// Build conditional (ternary) expression
static RbAstNode* build_rb_conditional(RbTranspiler* tp, TSNode cond_node) {
    RbTernaryNode* tern = (RbTernaryNode*)alloc_rb_ast_node(tp, RB_AST_NODE_TERNARY, cond_node, sizeof(RbTernaryNode));

    TSNode condition = ts_node_child_by_field_name(cond_node, "condition", 9);
    TSNode consequence = ts_node_child_by_field_name(cond_node, "consequence", 11);
    TSNode alternative = ts_node_child_by_field_name(cond_node, "alternative", 11);

    tern->condition = build_rb_expression(tp, condition);
    tern->true_expr = build_rb_expression(tp, consequence);
    tern->false_expr = build_rb_expression(tp, alternative);

    tern->base.type = &TYPE_ANY;
    return (RbAstNode*)tern;
}

// Build block (do..end or {}) 
static RbAstNode* build_rb_block_node(RbTranspiler* tp, TSNode block_node) {
    RbBlockNode* blk = (RbBlockNode*)alloc_rb_ast_node(tp, RB_AST_NODE_BLOCK, block_node, sizeof(RbBlockNode));
    blk->param_count = 0;

    const char* block_type = ts_node_type(block_node);

    // both "block" and "do_block" have parameters and body fields
    TSNode params_node = ts_node_child_by_field_name(block_node, "parameters", 10);
    TSNode body_node;

    if (strcmp(block_type, "block") == 0) {
        body_node = ts_node_child_by_field_name(block_node, "body", 4);
    } else {
        // do_block: body is body_statement
        body_node = ts_node_child_by_field_name(block_node, "body", 4);
    }

    // build block parameters
    if (!ts_node_is_null(params_node)) {
        RbAstNode* prev = NULL;
        uint32_t param_count = ts_node_named_child_count(params_node);
        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param = ts_node_named_child(params_node, i);
            const char* param_type = ts_node_type(param);

            RbParamNode* p = NULL;
            if (strcmp(param_type, "identifier") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                StrView name = rb_node_source(tp, param);
                p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
            } else if (strcmp(param_type, "splat_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                p->is_splat = true;
                if (ts_node_named_child_count(param) > 0) {
                    TSNode name_node = ts_node_named_child(param, 0);
                    StrView name = rb_node_source(tp, name_node);
                    p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                }
            } else if (strcmp(param_type, "block_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                p->is_block = true;
                if (ts_node_named_child_count(param) > 0) {
                    TSNode name_node = ts_node_named_child(param, 0);
                    StrView name = rb_node_source(tp, name_node);
                    p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                }
            } else {
                continue;
            }

            if (p) {
                blk->param_count++;
                if (!prev) {
                    blk->params = (RbAstNode*)p;
                } else {
                    prev->next = (RbAstNode*)p;
                }
                prev = (RbAstNode*)p;
            }
        }
    }

    // build body
    if (!ts_node_is_null(body_node)) {
        const char* body_type = ts_node_type(body_node);
        if (strcmp(body_type, "body_statement") == 0 || strcmp(body_type, "block_body") == 0) {
            blk->body = build_rb_body_statements(tp, body_node);
        } else {
            blk->body = build_rb_expression(tp, body_node);
        }
    }

    blk->base.type = &TYPE_ANY;
    return (RbAstNode*)blk;
}

// Build lambda expression (-> { } or lambda { })
static RbAstNode* build_rb_lambda(RbTranspiler* tp, TSNode lambda_node) {
    // lambda uses the same block infra
    TSNode body_node = ts_node_child_by_field_name(lambda_node, "body", 4);
    TSNode params_node = ts_node_child_by_field_name(lambda_node, "parameters", 10);

    RbBlockNode* blk = (RbBlockNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PROC_LAMBDA, lambda_node, sizeof(RbBlockNode));
    blk->param_count = 0;

    // build lambda parameters
    if (!ts_node_is_null(params_node)) {
        RbAstNode* prev = NULL;
        uint32_t param_count = ts_node_named_child_count(params_node);
        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param = ts_node_named_child(params_node, i);
            const char* param_type = ts_node_type(param);

            if (strcmp(param_type, "identifier") == 0) {
                RbParamNode* p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                StrView name = rb_node_source(tp, param);
                p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                blk->param_count++;
                if (!prev) { blk->params = (RbAstNode*)p; } else { prev->next = (RbAstNode*)p; }
                prev = (RbAstNode*)p;
            }
        }
    }

    // build body (block or do_block)
    if (!ts_node_is_null(body_node)) {
        RbAstNode* inner_block = build_rb_block_node(tp, body_node);
        if (inner_block && inner_block->node_type == RB_AST_NODE_BLOCK) {
            RbBlockNode* inner = (RbBlockNode*)inner_block;
            // merge inner block params/body if lambda params are empty
            if (blk->param_count == 0 && inner->param_count > 0) {
                blk->params = inner->params;
                blk->param_count = inner->param_count;
            }
            blk->body = inner->body;
        } else {
            blk->body = inner_block;
        }
    }

    blk->base.type = &TYPE_ANY;
    return (RbAstNode*)blk;
}

// ============================================================================
// Main expression dispatcher
// ============================================================================

RbAstNode* build_rb_expression(RbTranspiler* tp, TSNode expr_node) {
    if (ts_node_is_null(expr_node)) return NULL;

    const char* node_type = ts_node_type(expr_node);

    // identifiers and variables
    if (strcmp(node_type, "identifier") == 0) {
        return build_rb_identifier(tp, expr_node);
    }
    if (strcmp(node_type, "self") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_SELF, expr_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "instance_variable") == 0) {
        return build_rb_instance_variable(tp, expr_node);
    }
    if (strcmp(node_type, "class_variable") == 0) {
        return build_rb_class_variable(tp, expr_node);
    }
    if (strcmp(node_type, "global_variable") == 0) {
        return build_rb_global_variable(tp, expr_node);
    }
    if (strcmp(node_type, "constant") == 0) {
        return build_rb_constant(tp, expr_node);
    }

    // literals
    if (strcmp(node_type, "integer") == 0) {
        return build_rb_integer(tp, expr_node);
    }
    if (strcmp(node_type, "float") == 0) {
        return build_rb_float(tp, expr_node);
    }
    if (strcmp(node_type, "string") == 0 || strcmp(node_type, "bare_string") == 0 ||
        strcmp(node_type, "chained_string") == 0 || strcmp(node_type, "heredoc_body") == 0) {
        return build_rb_string(tp, expr_node);
    }
    if (strcmp(node_type, "heredoc_beginning") == 0) {
        // heredoc_beginning is the RHS of an assignment; the actual content is
        // in the next heredoc_body sibling of the containing statement.
        // Walk up to find the parent, then look for the next heredoc_body sibling.
        TSNode parent = ts_node_parent(expr_node);
        if (!ts_node_is_null(parent)) {
            TSNode grandparent = ts_node_parent(parent);
            if (!ts_node_is_null(grandparent)) {
                uint32_t gp_count = ts_node_named_child_count(grandparent);
                bool found_parent = false;
                for (uint32_t i = 0; i < gp_count; i++) {
                    TSNode sibling = ts_node_named_child(grandparent, i);
                    if (found_parent) {
                        const char* st = ts_node_type(sibling);
                        if (strcmp(st, "heredoc_body") == 0) {
                            return build_rb_string(tp, sibling);
                        }
                    }
                    if (ts_node_eq(sibling, parent)) {
                        found_parent = true;
                    }
                }
            }
        }
        // fallback: return empty string
        RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_LITERAL, expr_node, sizeof(RbLiteralNode));
        lit->literal_type = RB_LITERAL_STRING;
        lit->value.string_value = name_pool_create_len(tp->name_pool, "", 0);
        lit->base.type = &TYPE_STRING;
        return (RbAstNode*)lit;
    }
    if (strcmp(node_type, "true") == 0) {
        return build_rb_boolean(tp, expr_node, true);
    }
    if (strcmp(node_type, "false") == 0) {
        return build_rb_boolean(tp, expr_node, false);
    }
    if (strcmp(node_type, "nil") == 0) {
        return build_rb_nil(tp, expr_node);
    }
    if (strcmp(node_type, "simple_symbol") == 0 || strcmp(node_type, "delimited_symbol") == 0 ||
        strcmp(node_type, "bare_symbol") == 0 || strcmp(node_type, "hash_key_symbol") == 0) {
        return build_rb_symbol(tp, expr_node);
    }

    // regex literal: /pattern/
    if (strcmp(node_type, "regex") == 0) {
        // collect the pattern from string_content children inside _literal_contents
        char pattern_buf[1024];
        int pattern_len = 0;
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "string_content") == 0 || strcmp(ct, "escape_sequence") == 0) {
                StrView sv = rb_node_source(tp, child);
                int copy_len = (int)sv.length;
                if (pattern_len + copy_len < (int)sizeof(pattern_buf)) {
                    memcpy(pattern_buf + pattern_len, sv.str, copy_len);
                    pattern_len += copy_len;
                }
            }
        }
        pattern_buf[pattern_len] = '\0';

        RbLiteralNode* lit = (RbLiteralNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_LITERAL, expr_node, sizeof(RbLiteralNode));
        lit->literal_type = RB_LITERAL_REGEX;
        lit->value.string_value = name_pool_create_len(tp->name_pool, pattern_buf, pattern_len);
        lit->base.type = &TYPE_ANY;
        return (RbAstNode*)lit;
    }

    // operators
    if (strcmp(node_type, "binary") == 0) {
        return build_rb_binary_op(tp, expr_node);
    }
    if (strcmp(node_type, "unary") == 0) {
        return build_rb_unary_op(tp, expr_node);
    }
    // defined?(expr) and not(expr) — parenthesized_unary
    if (strcmp(node_type, "parenthesized_unary") == 0) {
        TSNode op_node = ts_node_child_by_field_name(expr_node, "operator", 8);
        TSNode operand_node = ts_node_child_by_field_name(expr_node, "operand", 7);
        if (!ts_node_is_null(op_node)) {
            StrView op_src = rb_node_source(tp, op_node);
            if (op_src.length >= 7 && memcmp(op_src.str, "defined", 7) == 0) {
                RbDefinedNode* def = (RbDefinedNode*)alloc_rb_ast_node(
                    tp, RB_AST_NODE_DEFINED, expr_node, sizeof(RbDefinedNode));
                // operand is parenthesized_statements — extract inner expression
                if (!ts_node_is_null(operand_node)) {
                    uint32_t ic = ts_node_named_child_count(operand_node);
                    if (ic > 0) {
                        def->operand = build_rb_expression(tp, ts_node_named_child(operand_node, 0));
                    }
                }
                def->base.type = &TYPE_STRING;
                return (RbAstNode*)def;
            }
            // 'not' operator
            if (op_src.length == 3 && memcmp(op_src.str, "not", 3) == 0) {
                RbUnaryNode* un = (RbUnaryNode*)alloc_rb_ast_node(
                    tp, RB_AST_NODE_UNARY_OP, expr_node, sizeof(RbUnaryNode));
                un->op = RB_OP_NOT;
                if (!ts_node_is_null(operand_node)) {
                    uint32_t ic = ts_node_named_child_count(operand_node);
                    if (ic > 0) {
                        un->operand = build_rb_expression(tp, ts_node_named_child(operand_node, 0));
                    }
                }
                un->base.type = &TYPE_BOOL;
                return (RbAstNode*)un;
            }
        }
    }

    // call, attribute, subscript
    if (strcmp(node_type, "call") == 0) {
        return build_rb_call(tp, expr_node);
    }
    if (strcmp(node_type, "element_reference") == 0) {
        return build_rb_element_reference(tp, expr_node);
    }
    if (strcmp(node_type, "scope_resolution") == 0) {
        // Module::Constant — treat as attribute access
        TSNode scope_node = ts_node_child_by_field_name(expr_node, "scope", 5);
        TSNode name_node = ts_node_child_by_field_name(expr_node, "name", 4);
        RbAttributeNode* attr = (RbAttributeNode*)alloc_rb_ast_node(tp, RB_AST_NODE_ATTRIBUTE, expr_node, sizeof(RbAttributeNode));
        if (!ts_node_is_null(scope_node)) {
            attr->object = build_rb_expression(tp, scope_node);
        }
        if (!ts_node_is_null(name_node)) {
            StrView name_src = rb_node_source(tp, name_node);
            attr->attr_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
        }
        attr->base.type = &TYPE_ANY;
        return (RbAstNode*)attr;
    }

    // collections
    if (strcmp(node_type, "array") == 0 || strcmp(node_type, "string_array") == 0 ||
        strcmp(node_type, "symbol_array") == 0) {
        return build_rb_array(tp, expr_node);
    }
    if (strcmp(node_type, "hash") == 0) {
        return build_rb_hash(tp, expr_node);
    }
    if (strcmp(node_type, "pair") == 0) {
        RbPairNode* pair = (RbPairNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PAIR, expr_node, sizeof(RbPairNode));
        TSNode key_node = ts_node_child_by_field_name(expr_node, "key", 3);
        TSNode val_node = ts_node_child_by_field_name(expr_node, "value", 5);
        if (!ts_node_is_null(key_node)) {
            const char* key_type = ts_node_type(key_node);
            if (strcmp(key_type, "hash_key_symbol") == 0) {
                pair->key = build_rb_symbol(tp, key_node);
            } else {
                pair->key = build_rb_expression(tp, key_node);
            }
        }
        if (!ts_node_is_null(val_node)) {
            pair->value = build_rb_expression(tp, val_node);
        }
        pair->base.type = &TYPE_ANY;
        return (RbAstNode*)pair;
    }

    // range
    if (strcmp(node_type, "range") == 0) {
        return build_rb_range(tp, expr_node);
    }

    // conditional (ternary)
    if (strcmp(node_type, "conditional") == 0) {
        return build_rb_conditional(tp, expr_node);
    }

    // lambda
    if (strcmp(node_type, "lambda") == 0) {
        return build_rb_lambda(tp, expr_node);
    }

    // block, do_block (when appearing as expressions)
    if (strcmp(node_type, "block") == 0 || strcmp(node_type, "do_block") == 0) {
        return build_rb_block_node(tp, expr_node);
    }

    // splat
    if (strcmp(node_type, "splat_argument") == 0) {
        RbSplatNode* sp = (RbSplatNode*)alloc_rb_ast_node(tp, RB_AST_NODE_SPLAT, expr_node, sizeof(RbSplatNode));
        if (ts_node_named_child_count(expr_node) > 0) {
            sp->operand = build_rb_expression(tp, ts_node_named_child(expr_node, 0));
        }
        sp->base.type = &TYPE_ANY;
        return (RbAstNode*)sp;
    }

    // parenthesized_statements — evaluate contents
    if (strcmp(node_type, "parenthesized_statements") == 0) {
        uint32_t child_count = ts_node_named_child_count(expr_node);
        if (child_count == 1) {
            return build_rb_expression(tp, ts_node_named_child(expr_node, 0));
        }
        // multiple statements — return last
        RbAstNode* last = NULL;
        for (uint32_t i = 0; i < child_count; i++) {
            last = build_rb_expression(tp, ts_node_named_child(expr_node, i));
        }
        return last;
    }

    // assignment can appear as expression
    if (strcmp(node_type, "assignment") == 0) {
        return build_rb_statement(tp, expr_node);
    }
    if (strcmp(node_type, "operator_assignment") == 0) {
        return build_rb_statement(tp, expr_node);
    }

    // if/unless/while/until modifiers can appear as expressions
    if (strcmp(node_type, "if_modifier") == 0 || strcmp(node_type, "unless_modifier") == 0 ||
        strcmp(node_type, "while_modifier") == 0 || strcmp(node_type, "until_modifier") == 0 ||
        strcmp(node_type, "rescue_modifier") == 0) {
        return build_rb_statement(tp, expr_node);
    }

    // yield
    if (strcmp(node_type, "yield") == 0) {
        RbYieldNode* yield = (RbYieldNode*)alloc_rb_ast_node(tp, RB_AST_NODE_YIELD, expr_node, sizeof(RbYieldNode));
        yield->arg_count = 0;
        uint32_t child_count = ts_node_named_child_count(expr_node);
        RbAstNode* prev = NULL;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(expr_node, i);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "argument_list") == 0) {
                uint32_t ac = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < ac; j++) {
                    RbAstNode* arg = build_rb_expression(tp, ts_node_named_child(child, j));
                    if (arg) {
                        yield->arg_count++;
                        if (!prev) { yield->args = arg; } else { prev->next = arg; }
                        prev = arg;
                    }
                }
            }
        }
        yield->base.type = &TYPE_ANY;
        return (RbAstNode*)yield;
    }

    log_debug("rb: unhandled expression type: %s", node_type);

    // control flow keywords that can appear in expression position (e.g., modifier-if)
    if (strcmp(node_type, "retry") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_RETRY, expr_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "break") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_BREAK, expr_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "next") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_NEXT, expr_node, sizeof(RbAstNode));
    }
    // begin/rescue/end as expression (e.g., x = begin...rescue...end)
    if (strcmp(node_type, "begin") == 0) {
        return build_rb_begin_rescue(tp, expr_node);
    }

    return NULL;
}

// ============================================================================
// Statement builders
// ============================================================================

// Build assignment statement (x = expr) or multiple assignment (a, b = 1, 2)
static RbAstNode* build_rb_assignment(RbTranspiler* tp, TSNode assign_node) {
    TSNode left_node = ts_node_child_by_field_name(assign_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(assign_node, "right", 5);

    // check for multiple assignment: left is left_assignment_list
    const char* left_type = ts_node_type(left_node);
    if (strcmp(left_type, "left_assignment_list") == 0) {
        RbMultiAssignmentNode* ma = (RbMultiAssignmentNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_MULTI_ASSIGNMENT, assign_node, sizeof(RbMultiAssignmentNode));
        ma->targets = NULL;
        ma->values = NULL;
        ma->target_count = 0;
        ma->value_count = 0;

        // build target list from left_assignment_list children
        RbAstNode* last_target = NULL;
        int left_count = (int)ts_node_named_child_count(left_node);
        for (int i = 0; i < left_count; i++) {
            TSNode child = ts_node_named_child(left_node, (uint32_t)i);
            RbAstNode* target = build_rb_expression(tp, child);
            if (target) {
                if (!ma->targets) ma->targets = target;
                else last_target->next = target;
                last_target = target;
                ma->target_count++;

                // define variables in scope
                if (target->node_type == RB_AST_NODE_IDENTIFIER) {
                    RbIdentifierNode* id = (RbIdentifierNode*)target;
                    rb_scope_define(tp, id->name, target, RB_VAR_LOCAL);
                }
            }
        }

        // build value list from right_assignment_list or single expression
        const char* right_type = ts_node_type(right_node);
        RbAstNode* last_value = NULL;
        if (strcmp(right_type, "right_assignment_list") == 0) {
            int right_count = (int)ts_node_named_child_count(right_node);
            for (int i = 0; i < right_count; i++) {
                TSNode child = ts_node_named_child(right_node, (uint32_t)i);
                RbAstNode* val = build_rb_expression(tp, child);
                if (val) {
                    if (!ma->values) ma->values = val;
                    else last_value->next = val;
                    last_value = val;
                    ma->value_count++;
                }
            }
        } else {
            // single value on right (e.g. a, b = array_expr)
            ma->values = build_rb_expression(tp, right_node);
            ma->value_count = 1;
        }

        ma->base.type = &TYPE_ANY;
        return (RbAstNode*)ma;
    }

    // simple assignment
    RbAssignmentNode* assign = (RbAssignmentNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_ASSIGNMENT, assign_node, sizeof(RbAssignmentNode));

    assign->target = build_rb_expression(tp, left_node);
    assign->value = build_rb_expression(tp, right_node);

    // define variable in scope
    if (assign->target && assign->target->node_type == RB_AST_NODE_IDENTIFIER) {
        RbIdentifierNode* id = (RbIdentifierNode*)assign->target;
        rb_scope_define(tp, id->name, assign->target, RB_VAR_LOCAL);
    }

    assign->base.type = &TYPE_ANY;
    return (RbAstNode*)assign;
}

// Build operator assignment (x += expr, etc.)
static RbAstNode* build_rb_op_assignment(RbTranspiler* tp, TSNode assign_node) {
    RbOpAssignmentNode* opa = (RbOpAssignmentNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_OP_ASSIGNMENT, assign_node, sizeof(RbOpAssignmentNode));

    TSNode left_node = ts_node_child_by_field_name(assign_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(assign_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(assign_node, "operator", 8);

    opa->target = build_rb_expression(tp, left_node);
    opa->value = build_rb_expression(tp, right_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_src = rb_node_source(tp, op_node);
        opa->op = rb_aug_operator(op_src.str, op_src.length);
    }

    opa->base.type = &TYPE_ANY;
    return (RbAstNode*)opa;
}

// Build return statement
static RbAstNode* build_rb_return(RbTranspiler* tp, TSNode ret_node) {
    RbReturnNode* ret = (RbReturnNode*)alloc_rb_ast_node(tp, RB_AST_NODE_RETURN, ret_node, sizeof(RbReturnNode));

    // return can have argument_list child
    uint32_t child_count = ts_node_named_child_count(ret_node);
    if (child_count > 0) {
        TSNode args = ts_node_named_child(ret_node, 0);
        const char* ct = ts_node_type(args);
        if (strcmp(ct, "argument_list") == 0) {
            uint32_t ac = ts_node_named_child_count(args);
            if (ac == 1) {
                ret->value = build_rb_expression(tp, ts_node_named_child(args, 0));
            } else if (ac > 1) {
                // multiple return values → array
                ret->value = build_rb_array(tp, args);
            }
        } else {
            ret->value = build_rb_expression(tp, args);
        }
    }

    ret->base.type = &TYPE_ANY;
    return (RbAstNode*)ret;
}

// Build method definition
static RbAstNode* build_rb_method_def(RbTranspiler* tp, TSNode method_node) {
    RbMethodDefNode* meth = (RbMethodDefNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_METHOD_DEF, method_node, sizeof(RbMethodDefNode));

    const char* method_type = ts_node_type(method_node);
    meth->is_class_method = (strcmp(method_type, "singleton_method") == 0);

    TSNode name_node = ts_node_child_by_field_name(method_node, "name", 4);
    TSNode params_node = ts_node_child_by_field_name(method_node, "parameters", 10);
    TSNode body_node = ts_node_child_by_field_name(method_node, "body", 4);

    if (!ts_node_is_null(name_node)) {
        StrView name_src = rb_node_source(tp, name_node);
        meth->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
    }

    // push method scope
    RbScope* method_scope = rb_scope_create(tp, RB_SCOPE_METHOD, tp->current_scope);
    method_scope->method = meth;
    rb_scope_push(tp, method_scope);

    // build parameters
    meth->param_count = 0;
    meth->required_count = 0;
    if (!ts_node_is_null(params_node)) {
        RbAstNode* prev = NULL;
        uint32_t param_count = ts_node_named_child_count(params_node);
        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param = ts_node_named_child(params_node, i);
            const char* param_type = ts_node_type(param);

            RbParamNode* p = NULL;

            if (strcmp(param_type, "identifier") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                StrView name = rb_node_source(tp, param);
                p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                meth->required_count++;
            } else if (strcmp(param_type, "optional_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_DEFAULT_PARAMETER, param, sizeof(RbParamNode));
                // name is first child, default is second
                TSNode pname = ts_node_child_by_field_name(param, "name", 4);
                TSNode pdefault = ts_node_child_by_field_name(param, "value", 5);
                if (!ts_node_is_null(pname)) {
                    StrView name = rb_node_source(tp, pname);
                    p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                }
                if (!ts_node_is_null(pdefault)) {
                    p->default_value = build_rb_expression(tp, pdefault);
                }
            } else if (strcmp(param_type, "splat_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                p->is_splat = true;
                meth->has_splat = true;
                if (ts_node_named_child_count(param) > 0) {
                    TSNode name = ts_node_named_child(param, 0);
                    StrView ns = rb_node_source(tp, name);
                    p->name = name_pool_create_len(tp->name_pool, ns.str, ns.length);
                }
            } else if (strcmp(param_type, "hash_splat_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                p->is_double_splat = true;
                meth->has_double_splat = true;
                if (ts_node_named_child_count(param) > 0) {
                    TSNode name = ts_node_named_child(param, 0);
                    StrView ns = rb_node_source(tp, name);
                    p->name = name_pool_create_len(tp->name_pool, ns.str, ns.length);
                }
            } else if (strcmp(param_type, "block_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_PARAMETER, param, sizeof(RbParamNode));
                p->is_block = true;
                meth->has_block_param = true;
                if (ts_node_named_child_count(param) > 0) {
                    TSNode name = ts_node_named_child(param, 0);
                    StrView ns = rb_node_source(tp, name);
                    p->name = name_pool_create_len(tp->name_pool, ns.str, ns.length);
                }
            } else if (strcmp(param_type, "keyword_parameter") == 0) {
                p = (RbParamNode*)alloc_rb_ast_node(tp, RB_AST_NODE_DEFAULT_PARAMETER, param, sizeof(RbParamNode));
                TSNode pname = ts_node_child_by_field_name(param, "name", 4);
                TSNode pdefault = ts_node_child_by_field_name(param, "value", 5);
                if (!ts_node_is_null(pname)) {
                    StrView name = rb_node_source(tp, pname);
                    p->name = name_pool_create_len(tp->name_pool, name.str, name.length);
                }
                if (!ts_node_is_null(pdefault)) {
                    p->default_value = build_rb_expression(tp, pdefault);
                }
            } else {
                continue;
            }

            if (p) {
                // define parameter in scope
                if (p->name) {
                    rb_scope_define(tp, p->name, (RbAstNode*)p, RB_VAR_LOCAL);
                }
                meth->param_count++;
                if (!prev) {
                    meth->params = (RbAstNode*)p;
                } else {
                    prev->next = (RbAstNode*)p;
                }
                prev = (RbAstNode*)p;
            }
        }
    }

    // build body
    if (!ts_node_is_null(body_node)) {
        const char* body_type = ts_node_type(body_node);
        if (strcmp(body_type, "body_statement") == 0) {
            meth->body = build_rb_body_statements(tp, body_node);
        } else {
            // single expression body
            meth->body = build_rb_expression(tp, body_node);
        }
    }

    rb_scope_pop(tp);

    // define method in enclosing scope
    if (meth->name) {
        rb_scope_define(tp, meth->name, (RbAstNode*)meth, RB_VAR_LOCAL);
    }

    meth->base.type = &TYPE_ANY;
    return (RbAstNode*)meth;
}

// Build class definition
static RbAstNode* build_rb_class_def(RbTranspiler* tp, TSNode class_node) {
    RbClassDefNode* cls = (RbClassDefNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_CLASS_DEF, class_node, sizeof(RbClassDefNode));

    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    TSNode super_node = ts_node_child_by_field_name(class_node, "superclass", 10);
    TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);

    if (!ts_node_is_null(name_node)) {
        StrView name_src = rb_node_source(tp, name_node);
        cls->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
    }

    // superclass
    if (!ts_node_is_null(super_node)) {
        // superclass node wraps the actual expression
        uint32_t sc = ts_node_named_child_count(super_node);
        if (sc > 0) {
            cls->superclass = build_rb_expression(tp, ts_node_named_child(super_node, 0));
        }
    }

    // push class scope
    RbScope* class_scope = rb_scope_create(tp, RB_SCOPE_CLASS, tp->current_scope);
    rb_scope_push(tp, class_scope);

    if (!ts_node_is_null(body_node)) {
        cls->body = build_rb_body_statements(tp, body_node);
    }

    rb_scope_pop(tp);

    // define class in enclosing scope
    if (cls->name) {
        rb_scope_define(tp, cls->name, (RbAstNode*)cls, RB_VAR_CONST);
    }

    cls->base.type = &TYPE_ANY;
    return (RbAstNode*)cls;
}

// Build module definition
static RbAstNode* build_rb_module_def(RbTranspiler* tp, TSNode mod_node) {
    RbModuleDefNode* mod = (RbModuleDefNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_MODULE_DEF, mod_node, sizeof(RbModuleDefNode));

    TSNode name_node = ts_node_child_by_field_name(mod_node, "name", 4);
    TSNode body_node = ts_node_child_by_field_name(mod_node, "body", 4);

    if (!ts_node_is_null(name_node)) {
        StrView name_src = rb_node_source(tp, name_node);
        mod->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
    }

    RbScope* mod_scope = rb_scope_create(tp, RB_SCOPE_MODULE, tp->current_scope);
    rb_scope_push(tp, mod_scope);

    if (!ts_node_is_null(body_node)) {
        mod->body = build_rb_body_statements(tp, body_node);
    }

    rb_scope_pop(tp);

    if (mod->name) {
        rb_scope_define(tp, mod->name, (RbAstNode*)mod, RB_VAR_MODULE);
    }

    mod->base.type = &TYPE_ANY;
    return (RbAstNode*)mod;
}

// Build if/unless statement
static RbAstNode* build_rb_if_statement(RbTranspiler* tp, TSNode if_node, bool is_unless) {
    RbIfNode* ifs = (RbIfNode*)alloc_rb_ast_node(
        tp, is_unless ? RB_AST_NODE_UNLESS : RB_AST_NODE_IF, if_node, sizeof(RbIfNode));
    ifs->is_unless = is_unless;
    ifs->is_modifier = false;

    TSNode cond_node = ts_node_child_by_field_name(if_node, "condition", 9);
    TSNode then_node = ts_node_child_by_field_name(if_node, "consequence", 11);
    TSNode alt_node = ts_node_child_by_field_name(if_node, "alternative", 11);

    ifs->condition = build_rb_expression(tp, cond_node);

    // consequence is a "then" node — iterate its children
    if (!ts_node_is_null(then_node)) {
        ifs->then_body = build_rb_body_statements(tp, then_node);
    }

    // alternative can be "else" or "elsif"
    if (!ts_node_is_null(alt_node)) {
        const char* alt_type = ts_node_type(alt_node);
        if (strcmp(alt_type, "elsif") == 0) {
            // elsif is like another if node
            ifs->elsif_chain = build_rb_if_statement(tp, alt_node, false);
        } else if (strcmp(alt_type, "else") == 0) {
            ifs->else_body = build_rb_body_statements(tp, alt_node);
        }
    }

    ifs->base.type = &TYPE_ANY;
    return (RbAstNode*)ifs;
}

// Build if_modifier / unless_modifier  (expr if cond / expr unless cond)
static RbAstNode* build_rb_modifier_if(RbTranspiler* tp, TSNode mod_node, bool is_unless) {
    RbIfNode* ifs = (RbIfNode*)alloc_rb_ast_node(
        tp, is_unless ? RB_AST_NODE_UNLESS : RB_AST_NODE_IF, mod_node, sizeof(RbIfNode));
    ifs->is_unless = is_unless;
    ifs->is_modifier = true;

    // modifier form: body condition (two named children)
    TSNode body_node = ts_node_child_by_field_name(mod_node, "body", 4);
    TSNode cond_node = ts_node_child_by_field_name(mod_node, "condition", 9);

    ifs->condition = build_rb_expression(tp, cond_node);
    ifs->then_body = build_rb_expression(tp, body_node);

    ifs->base.type = &TYPE_ANY;
    return (RbAstNode*)ifs;
}

// Build while/until statement
static RbAstNode* build_rb_while_statement(RbTranspiler* tp, TSNode wh_node, bool is_until) {
    RbWhileNode* wh = (RbWhileNode*)alloc_rb_ast_node(
        tp, is_until ? RB_AST_NODE_UNTIL : RB_AST_NODE_WHILE, wh_node, sizeof(RbWhileNode));
    wh->is_until = is_until;

    TSNode cond_node = ts_node_child_by_field_name(wh_node, "condition", 9);
    TSNode body_node = ts_node_child_by_field_name(wh_node, "body", 4);

    wh->condition = build_rb_expression(tp, cond_node);

    if (!ts_node_is_null(body_node)) {
        wh->body = build_rb_body_statements(tp, body_node);
    }

    wh->base.type = &TYPE_ANY;
    return (RbAstNode*)wh;
}

// Build for statement
static RbAstNode* build_rb_for_statement(RbTranspiler* tp, TSNode for_node) {
    RbForNode* f = (RbForNode*)alloc_rb_ast_node(tp, RB_AST_NODE_FOR, for_node, sizeof(RbForNode));

    TSNode pattern_node = ts_node_child_by_field_name(for_node, "pattern", 7);
    TSNode value_node = ts_node_child_by_field_name(for_node, "value", 5);
    TSNode body_node = ts_node_child_by_field_name(for_node, "body", 4);

    f->variable = build_rb_expression(tp, pattern_node);

    // value is "in" node — get its child
    if (!ts_node_is_null(value_node)) {
        uint32_t vc = ts_node_named_child_count(value_node);
        if (vc > 0) {
            f->collection = build_rb_expression(tp, ts_node_named_child(value_node, 0));
        }
    }

    if (!ts_node_is_null(body_node)) {
        f->body = build_rb_body_statements(tp, body_node);
    }

    f->base.type = &TYPE_ANY;
    return (RbAstNode*)f;
}

// Build case/when statement
static RbAstNode* build_rb_case_statement(RbTranspiler* tp, TSNode case_node) {
    RbCaseNode* cs = (RbCaseNode*)alloc_rb_ast_node(tp, RB_AST_NODE_CASE, case_node, sizeof(RbCaseNode));

    TSNode value_node = ts_node_child_by_field_name(case_node, "value", 5);
    if (!ts_node_is_null(value_node)) {
        cs->subject = build_rb_expression(tp, value_node);
    }

    // iterate children for "when" and "else" nodes
    uint32_t child_count = ts_node_named_child_count(case_node);
    RbAstNode* prev_when = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(case_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "when") == 0) {
            RbWhenNode* w = (RbWhenNode*)alloc_rb_ast_node(tp, RB_AST_NODE_WHEN, child, sizeof(RbWhenNode));

            // when has pattern field (contains patterns)
            TSNode pattern_node = ts_node_child_by_field_name(child, "pattern", 7);
            if (!ts_node_is_null(pattern_node)) {
                // pattern node contains comma-separated patterns
                uint32_t pc = ts_node_named_child_count(pattern_node);
                RbAstNode* prev_pat = NULL;
                for (uint32_t j = 0; j < pc; j++) {
                    RbAstNode* pat = build_rb_expression(tp, ts_node_named_child(pattern_node, j));
                    if (pat) {
                        if (!prev_pat) { w->patterns = pat; } else { prev_pat->next = pat; }
                        prev_pat = pat;
                    }
                }
            }

            // body
            TSNode when_body = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(when_body)) {
                w->body = build_rb_body_statements(tp, when_body);
            }

            w->base.type = &TYPE_ANY;
            if (!prev_when) { cs->whens = (RbAstNode*)w; } else { prev_when->next = (RbAstNode*)w; }
            prev_when = (RbAstNode*)w;
        } else if (strcmp(child_type, "else") == 0) {
            cs->else_body = build_rb_body_statements(tp, child);
        }
    }

    cs->base.type = &TYPE_ANY;
    return (RbAstNode*)cs;
}

// Build begin/rescue/ensure
static RbAstNode* build_rb_begin_rescue(RbTranspiler* tp, TSNode begin_node) {
    RbBeginRescueNode* br = (RbBeginRescueNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_BEGIN_RESCUE, begin_node, sizeof(RbBeginRescueNode));

    uint32_t child_count = ts_node_named_child_count(begin_node);
    RbAstNode* body_prev = NULL;
    RbAstNode* rescue_prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(begin_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "rescue") == 0) {
            RbRescueNode* resc = (RbRescueNode*)alloc_rb_ast_node(
                tp, RB_AST_NODE_RESCUE, child, sizeof(RbRescueNode));

            // exceptions field
            TSNode exc_node = ts_node_child_by_field_name(child, "exceptions", 10);
            if (!ts_node_is_null(exc_node)) {
                uint32_t ec = ts_node_named_child_count(exc_node);
                RbAstNode* prev = NULL;
                for (uint32_t j = 0; j < ec; j++) {
                    RbAstNode* exc = build_rb_expression(tp, ts_node_named_child(exc_node, j));
                    if (exc) {
                        if (!prev) { resc->exception_classes = exc; } else { prev->next = exc; }
                        prev = exc;
                    }
                }
            }

            // variable field (=> var)
            TSNode var_node = ts_node_child_by_field_name(child, "variable", 8);
            if (!ts_node_is_null(var_node)) {
                // exception_variable wraps an identifier
                uint32_t vc = ts_node_named_child_count(var_node);
                if (vc > 0) {
                    TSNode vn = ts_node_named_child(var_node, 0);
                    StrView vs = rb_node_source(tp, vn);
                    resc->variable_name = name_pool_create_len(tp->name_pool, vs.str, vs.length);
                }
            }

            // body
            TSNode body_node = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(body_node)) {
                resc->body = build_rb_body_statements(tp, body_node);
            }

            resc->base.type = &TYPE_ANY;
            if (!rescue_prev) { br->rescues = (RbAstNode*)resc; } else { rescue_prev->next = (RbAstNode*)resc; }
            rescue_prev = (RbAstNode*)resc;
        } else if (strcmp(child_type, "ensure") == 0) {
            br->ensure_body = build_rb_body_statements(tp, child);
        } else if (strcmp(child_type, "else") == 0) {
            br->else_body = build_rb_body_statements(tp, child);
        } else {
            // body statement
            RbAstNode* stmt = build_rb_statement(tp, child);
            if (stmt) {
                if (!body_prev) { br->body = stmt; } else { body_prev->next = stmt; }
                body_prev = stmt;
            }
        }
    }

    br->base.type = &TYPE_ANY;
    return (RbAstNode*)br;
}

// Build body_statement, then, else, do — iterate children as statements
static RbAstNode* build_rb_body_statements(RbTranspiler* tp, TSNode body_node) {
    uint32_t child_count = ts_node_named_child_count(body_node);
    RbAstNode* first = NULL;
    RbAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(body_node, i);
        const char* child_type = ts_node_type(child);

        // skip certain structural nodes that appear inside body_statement
        if (strcmp(child_type, "rescue") == 0 || strcmp(child_type, "ensure") == 0 ||
            strcmp(child_type, "else") == 0 || strcmp(child_type, "empty_statement") == 0 ||
            strcmp(child_type, "heredoc_body") == 0) {
            continue;
        }

        RbAstNode* stmt = build_rb_statement(tp, child);
        if (stmt) {
            if (!prev) { first = stmt; } else { prev->next = stmt; }
            prev = stmt;
        }
    }

    return first;
}

// ============================================================================
// Main statement dispatcher
// ============================================================================

RbAstNode* build_rb_statement(RbTranspiler* tp, TSNode stmt_node) {
    if (ts_node_is_null(stmt_node)) return NULL;

    const char* node_type = ts_node_type(stmt_node);

    // skip comments
    if (strcmp(node_type, "comment") == 0) return NULL;

    // assignment
    if (strcmp(node_type, "assignment") == 0) {
        return build_rb_assignment(tp, stmt_node);
    }
    if (strcmp(node_type, "operator_assignment") == 0) {
        return build_rb_op_assignment(tp, stmt_node);
    }

    // expression statement — bare expressions
    if (strcmp(node_type, "expression_statement") == 0) {
        uint32_t child_count = ts_node_named_child_count(stmt_node);
        if (child_count == 0) return NULL;
        return build_rb_expression(tp, ts_node_named_child(stmt_node, 0));
    }

    // control flow
    if (strcmp(node_type, "if") == 0) {
        return build_rb_if_statement(tp, stmt_node, false);
    }
    if (strcmp(node_type, "unless") == 0) {
        return build_rb_if_statement(tp, stmt_node, true);
    }
    if (strcmp(node_type, "if_modifier") == 0) {
        return build_rb_modifier_if(tp, stmt_node, false);
    }
    if (strcmp(node_type, "unless_modifier") == 0) {
        return build_rb_modifier_if(tp, stmt_node, true);
    }
    if (strcmp(node_type, "while") == 0) {
        return build_rb_while_statement(tp, stmt_node, false);
    }
    if (strcmp(node_type, "until") == 0) {
        return build_rb_while_statement(tp, stmt_node, true);
    }
    if (strcmp(node_type, "while_modifier") == 0 || strcmp(node_type, "until_modifier") == 0) {
        bool is_until = strcmp(node_type, "until_modifier") == 0;
        RbWhileNode* wh = (RbWhileNode*)alloc_rb_ast_node(
            tp, is_until ? RB_AST_NODE_UNTIL : RB_AST_NODE_WHILE, stmt_node, sizeof(RbWhileNode));
        wh->is_until = is_until;
        TSNode body_node = ts_node_child_by_field_name(stmt_node, "body", 4);
        TSNode cond_node = ts_node_child_by_field_name(stmt_node, "condition", 9);
        wh->condition = build_rb_expression(tp, cond_node);
        wh->body = build_rb_expression(tp, body_node);
        wh->base.type = &TYPE_ANY;
        return (RbAstNode*)wh;
    }
    if (strcmp(node_type, "for") == 0) {
        return build_rb_for_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "case") == 0) {
        return build_rb_case_statement(tp, stmt_node);
    }

    // definitions
    if (strcmp(node_type, "method") == 0 || strcmp(node_type, "singleton_method") == 0) {
        return build_rb_method_def(tp, stmt_node);
    }
    if (strcmp(node_type, "class") == 0) {
        return build_rb_class_def(tp, stmt_node);
    }
    if (strcmp(node_type, "module") == 0) {
        return build_rb_module_def(tp, stmt_node);
    }

    // begin/rescue/ensure
    if (strcmp(node_type, "begin") == 0) {
        return build_rb_begin_rescue(tp, stmt_node);
    }

    // simple statements
    if (strcmp(node_type, "return") == 0) {
        return build_rb_return(tp, stmt_node);
    }
    if (strcmp(node_type, "break") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_BREAK, stmt_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "next") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_NEXT, stmt_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "retry") == 0) {
        return alloc_rb_ast_node(tp, RB_AST_NODE_RETRY, stmt_node, sizeof(RbAstNode));
    }
    if (strcmp(node_type, "yield") == 0) {
        return build_rb_expression(tp, stmt_node);
    }

    // body_statement (can appear as standalone)
    if (strcmp(node_type, "body_statement") == 0) {
        return build_rb_body_statements(tp, stmt_node);
    }

    // rescue_modifier: expr rescue expr
    if (strcmp(node_type, "rescue_modifier") == 0) {
        // treat as begin/rescue simplified form
        RbBeginRescueNode* br = (RbBeginRescueNode*)alloc_rb_ast_node(
            tp, RB_AST_NODE_BEGIN_RESCUE, stmt_node, sizeof(RbBeginRescueNode));
        TSNode body_node = ts_node_child_by_field_name(stmt_node, "body", 4);
        TSNode handler = ts_node_child_by_field_name(stmt_node, "handler", 7);
        br->body = build_rb_expression(tp, body_node);
        if (!ts_node_is_null(handler)) {
            RbRescueNode* resc = (RbRescueNode*)alloc_rb_ast_node(
                tp, RB_AST_NODE_RESCUE, handler, sizeof(RbRescueNode));
            resc->body = build_rb_expression(tp, handler);
            resc->base.type = &TYPE_ANY;
            br->rescues = (RbAstNode*)resc;
        }
        br->base.type = &TYPE_ANY;
        return (RbAstNode*)br;
    }

    // fallback: try as expression
    RbAstNode* expr = build_rb_expression(tp, stmt_node);
    if (expr) {
        return expr;
    }

    log_debug("rb: unhandled statement type: %s", node_type);
    return NULL;
}

// ============================================================================
// Main entry point
// ============================================================================

RbAstNode* build_rb_ast(RbTranspiler* tp, TSNode root) {
    const char* root_type = ts_node_type(root);

    if (strcmp(root_type, "program") != 0) {
        log_error("rb: expected 'program' root node, got '%s'", root_type);
        return NULL;
    }

    RbProgramNode* prog = (RbProgramNode*)alloc_rb_ast_node(
        tp, RB_AST_NODE_PROGRAM, root, sizeof(RbProgramNode));

    uint32_t child_count = ts_node_named_child_count(root);
    RbAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(root, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "comment") == 0) continue;

        RbAstNode* stmt = build_rb_statement(tp, child);
        if (stmt) {
            if (!prev) {
                prog->body = stmt;
            } else {
                prev->next = stmt;
            }
            prev = stmt;
        }
    }

    return (RbAstNode*)prog;
}
