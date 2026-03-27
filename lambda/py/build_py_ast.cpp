#include "py_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

// External Tree-sitter Python parser
extern "C" {
    const TSLanguage *tree_sitter_python(void);
}

// Utility macro to get source text from a Tree-sitter node
#define py_node_source(transpiler, node) {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

// Forward declarations
PyAstNode* build_py_block(PyTranspiler* tp, TSNode block_node);
PyAstNode* build_py_if_statement(PyTranspiler* tp, TSNode if_node);
PyAstNode* build_py_while_statement(PyTranspiler* tp, TSNode while_node);
PyAstNode* build_py_for_statement(PyTranspiler* tp, TSNode for_node);
PyAstNode* build_py_try_statement(PyTranspiler* tp, TSNode try_node);
PyAstNode* build_py_with_statement(PyTranspiler* tp, TSNode with_node);
PyAstNode* build_py_class_def(PyTranspiler* tp, TSNode class_node);
PyAstNode* build_py_comparison(PyTranspiler* tp, TSNode comp_node);
PyAstNode* build_py_string(PyTranspiler* tp, TSNode string_node);
PyAstNode* build_py_fstring(PyTranspiler* tp, TSNode string_node);
PyAstNode* build_py_list_comprehension(PyTranspiler* tp, TSNode comp_node, PyAstNodeType comp_type);
PyAstNode* build_py_lambda(PyTranspiler* tp, TSNode lambda_node);
PyAstNode* build_py_conditional_expr(PyTranspiler* tp, TSNode cond_node);
PyAstNode* build_py_dict(PyTranspiler* tp, TSNode dict_node);
PyAstNode* build_py_slice(PyTranspiler* tp, TSNode slice_node);
PyAstNode* build_py_decorated_definition(PyTranspiler* tp, TSNode dec_node);
PyAstNode* build_py_parameters(PyTranspiler* tp, TSNode params_node);

// Allocate a Python AST node from the pool
PyAstNode* alloc_py_ast_node(PyTranspiler* tp, PyAstNodeType node_type, TSNode node, size_t size) {
    PyAstNode* ast_node = (PyAstNode*)pool_alloc(tp->ast_pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;
    ast_node->node = node;
    return ast_node;
}

// Convert Python operator string to PyOperator enum
PyOperator py_operator_from_string(const char* op_str, size_t len) {
    if (len == 1) {
        switch (op_str[0]) {
            case '+': return PY_OP_ADD;
            case '-': return PY_OP_SUB;
            case '*': return PY_OP_MUL;
            case '/': return PY_OP_DIV;
            case '%': return PY_OP_MOD;
            case '@': return PY_OP_MATMUL;
            case '<': return PY_OP_LT;
            case '>': return PY_OP_GT;
            case '~': return PY_OP_BIT_NOT;
            case '&': return PY_OP_BIT_AND;
            case '|': return PY_OP_BIT_OR;
            case '^': return PY_OP_BIT_XOR;
        }
    } else if (len == 2) {
        if (strncmp(op_str, "==", 2) == 0) return PY_OP_EQ;
        if (strncmp(op_str, "!=", 2) == 0) return PY_OP_NE;
        if (strncmp(op_str, "<=", 2) == 0) return PY_OP_LE;
        if (strncmp(op_str, ">=", 2) == 0) return PY_OP_GE;
        if (strncmp(op_str, "**", 2) == 0) return PY_OP_POW;
        if (strncmp(op_str, "//", 2) == 0) return PY_OP_FLOOR_DIV;
        if (strncmp(op_str, "<<", 2) == 0) return PY_OP_LSHIFT;
        if (strncmp(op_str, ">>", 2) == 0) return PY_OP_RSHIFT;
        if (strncmp(op_str, "in", 2) == 0) return PY_OP_IN;
        if (strncmp(op_str, "is", 2) == 0) return PY_OP_IS;
        if (strncmp(op_str, "or", 2) == 0) return PY_OP_OR;
    } else if (len == 3) {
        if (strncmp(op_str, "and", 3) == 0) return PY_OP_AND;
        if (strncmp(op_str, "not", 3) == 0) return PY_OP_NOT;
    } else if (len == 6) {
        if (strncmp(op_str, "not in", 6) == 0) return PY_OP_NOT_IN;
        if (strncmp(op_str, "is not", 6) == 0) return PY_OP_IS_NOT;
    }

    log_error("py: unknown operator: %.*s", (int)len, op_str);
    return PY_OP_ADD;
}

// Convert Python augmented assignment operator string to PyOperator enum
PyOperator py_augmented_operator_from_string(const char* op_str, size_t len) {
    if (len == 2) {
        if (strncmp(op_str, "+=", 2) == 0) return PY_OP_ADD_ASSIGN;
        if (strncmp(op_str, "-=", 2) == 0) return PY_OP_SUB_ASSIGN;
        if (strncmp(op_str, "*=", 2) == 0) return PY_OP_MUL_ASSIGN;
        if (strncmp(op_str, "/=", 2) == 0) return PY_OP_DIV_ASSIGN;
        if (strncmp(op_str, "%=", 2) == 0) return PY_OP_MOD_ASSIGN;
        if (strncmp(op_str, "@=", 2) == 0) return PY_OP_MATMUL_ASSIGN;
        if (strncmp(op_str, "&=", 2) == 0) return PY_OP_BIT_AND_ASSIGN;
        if (strncmp(op_str, "|=", 2) == 0) return PY_OP_BIT_OR_ASSIGN;
        if (strncmp(op_str, "^=", 2) == 0) return PY_OP_BIT_XOR_ASSIGN;
    } else if (len == 3) {
        if (strncmp(op_str, "//=", 3) == 0) return PY_OP_FLOOR_DIV_ASSIGN;
        if (strncmp(op_str, "**=", 3) == 0) return PY_OP_POW_ASSIGN;
        if (strncmp(op_str, "<<=", 3) == 0) return PY_OP_LSHIFT_ASSIGN;
        if (strncmp(op_str, ">>=", 3) == 0) return PY_OP_RSHIFT_ASSIGN;
    }

    log_error("py: unknown augmented assignment operator: %.*s", (int)len, op_str);
    return PY_OP_ADD_ASSIGN;
}

// ---- Literal builders ----

// Build Python integer literal
static PyAstNode* build_py_integer(PyTranspiler* tp, TSNode int_node) {
    PyLiteralNode* literal = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, int_node, sizeof(PyLiteralNode));
    literal->literal_type = PY_LITERAL_INT;

    StrView source = py_node_source(tp, int_node);
    char* temp_str = (char*)malloc(source.length + 1);
    if (temp_str) {
        memcpy(temp_str, source.str, source.length);
        temp_str[source.length] = '\0';

        // handle hex, octal, binary prefixes
        if (source.length > 2 && temp_str[0] == '0') {
            char prefix = temp_str[1];
            if (prefix == 'x' || prefix == 'X') {
                literal->value.int_value = strtoll(temp_str, NULL, 16);
            } else if (prefix == 'o' || prefix == 'O') {
                literal->value.int_value = strtoll(temp_str, NULL, 8);
            } else if (prefix == 'b' || prefix == 'B') {
                literal->value.int_value = strtoll(temp_str, NULL, 2);
            } else {
                literal->value.int_value = strtoll(temp_str, NULL, 10);
            }
        } else {
            literal->value.int_value = strtoll(temp_str, NULL, 10);
        }
        free(temp_str);
    }

    literal->base.type = &TYPE_INT;
    return (PyAstNode*)literal;
}

// Build Python float literal
static PyAstNode* build_py_float(PyTranspiler* tp, TSNode float_node) {
    PyLiteralNode* literal = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, float_node, sizeof(PyLiteralNode));
    literal->literal_type = PY_LITERAL_FLOAT;

    StrView source = py_node_source(tp, float_node);
    char* temp_str = (char*)malloc(source.length + 1);
    if (temp_str) {
        memcpy(temp_str, source.str, source.length);
        temp_str[source.length] = '\0';
        literal->value.float_value = strtod(temp_str, NULL);
        free(temp_str);
    }

    literal->base.type = &TYPE_FLOAT;
    return (PyAstNode*)literal;
}

// Build Python boolean literal
static PyAstNode* build_py_boolean(PyTranspiler* tp, TSNode bool_node, bool value) {
    PyLiteralNode* literal = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, bool_node, sizeof(PyLiteralNode));
    literal->literal_type = PY_LITERAL_BOOLEAN;
    literal->value.boolean_value = value;
    literal->base.type = &TYPE_BOOL;
    return (PyAstNode*)literal;
}

// Build Python None literal
static PyAstNode* build_py_none(PyTranspiler* tp, TSNode none_node) {
    PyLiteralNode* literal = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, none_node, sizeof(PyLiteralNode));
    literal->literal_type = PY_LITERAL_NONE;
    literal->base.type = &TYPE_NULL;
    return (PyAstNode*)literal;
}

// ---- Identifier builder ----

static PyAstNode* build_py_identifier(PyTranspiler* tp, TSNode id_node) {
    if (ts_node_is_null(id_node)) return NULL;

    PyIdentifierNode* id = (PyIdentifierNode*)alloc_py_ast_node(tp, PY_AST_NODE_IDENTIFIER, id_node, sizeof(PyIdentifierNode));

    StrView source = py_node_source(tp, id_node);
    if (source.length == 0) return NULL;

    char* temp_str = (char*)malloc(source.length + 1);
    if (!temp_str) return NULL;
    memcpy(temp_str, source.str, source.length);
    temp_str[source.length] = '\0';

    id->name = name_pool_create_len(tp->name_pool, temp_str, source.length);
    free(temp_str);
    if (!id->name) return NULL;

    // scope lookup
    id->entry = py_scope_lookup(tp, id->name);
    if (id->entry) {
        id->base.type = id->entry->node->type;
    } else {
        id->base.type = &TYPE_ANY;
    }

    return (PyAstNode*)id;
}

// ---- String builder ----

// Decode a Python escape sequence, return number of chars consumed from input
static int py_decode_escape(const char* src, size_t src_len, char* out) {
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
        default:   *out = src[1]; return 2;
    }
}

PyAstNode* build_py_string(PyTranspiler* tp, TSNode string_node) {
    // tree-sitter-python strings have children: string_start, string_content/interpolation, string_end
    uint32_t child_count = ts_node_named_child_count(string_node);

    // check if it's an f-string by looking at string_start
    uint32_t total_children = ts_node_child_count(string_node);
    if (total_children > 0) {
        TSNode start = ts_node_child(string_node, 0);
        StrView start_src = py_node_source(tp, start);
        // f-string starts with f" or f' or F" or F'
        for (size_t i = 0; i < start_src.length; i++) {
            if (start_src.str[i] == 'f' || start_src.str[i] == 'F') {
                return build_py_fstring(tp, string_node);
            }
        }
    }

    // regular string — just extract content
    PyLiteralNode* literal = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, string_node, sizeof(PyLiteralNode));
    literal->literal_type = PY_LITERAL_STRING;

    // collect all string_content children into a buffer
    char buf[4096];
    size_t buf_len = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(string_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "string_content") == 0) {
            StrView content = py_node_source(tp, child);
            // look for escape_sequence children inside string_content
            uint32_t inner_count = ts_node_named_child_count(child);
            if (inner_count == 0) {
                // plain content, copy directly
                size_t copy_len = content.length;
                if (buf_len + copy_len >= sizeof(buf)) copy_len = sizeof(buf) - buf_len - 1;
                memcpy(buf + buf_len, content.str, copy_len);
                buf_len += copy_len;
            } else {
                // has escape sequences — walk byte by byte using child positions
                uint32_t pos = ts_node_start_byte(child);
                uint32_t end = ts_node_end_byte(child);
                uint32_t esc_idx = 0;

                while (pos < end && buf_len < sizeof(buf) - 1) {
                    // check if current position matches an escape_sequence child
                    bool is_escape = false;
                    if (esc_idx < inner_count) {
                        TSNode esc_child = ts_node_named_child(child, esc_idx);
                        if (strcmp(ts_node_type(esc_child), "escape_sequence") == 0) {
                            uint32_t esc_start = ts_node_start_byte(esc_child);
                            uint32_t esc_end = ts_node_end_byte(esc_child);

                            // copy any text before the escape
                            if (pos < esc_start) {
                                size_t pre_len = esc_start - pos;
                                if (buf_len + pre_len >= sizeof(buf)) pre_len = sizeof(buf) - buf_len - 1;
                                memcpy(buf + buf_len, tp->source + pos, pre_len);
                                buf_len += pre_len;
                            }

                            // decode escape
                            const char* esc_src = tp->source + esc_start;
                            size_t esc_len = esc_end - esc_start;
                            char decoded;
                            if (py_decode_escape(esc_src, esc_len, &decoded) > 0) {
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
            StrView esc_src = py_node_source(tp, child);
            char decoded;
            if (py_decode_escape(esc_src.str, esc_src.length, &decoded) > 0) {
                if (buf_len < sizeof(buf) - 1) buf[buf_len++] = decoded;
            }
        }
    }

    // if no named children were found, fall back to extracting from raw source (simple string)
    if (child_count == 0) {
        StrView source = py_node_source(tp, string_node);
        // strip outer quotes
        const char* start = source.str;
        size_t len = source.length;
        if (len >= 6 && (strncmp(start, "\"\"\"", 3) == 0 || strncmp(start, "'''", 3) == 0)) {
            start += 3; len -= 6; // triple-quoted
        } else if (len >= 2) {
            // skip prefix chars (r, b, etc.) and quote
            while (len > 0 && *start != '\'' && *start != '"') { start++; len--; }
            if (len >= 2) { start++; len -= 2; }
        }
        size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, start, copy_len);
        buf_len = copy_len;
    }

    buf[buf_len] = '\0';
    literal->value.string_value = name_pool_create_len(tp->name_pool, buf, buf_len);
    literal->base.type = &TYPE_STRING;
    return (PyAstNode*)literal;
}

// Build f-string node
PyAstNode* build_py_fstring(PyTranspiler* tp, TSNode string_node) {
    PyFStringNode* fstr = (PyFStringNode*)alloc_py_ast_node(tp, PY_AST_NODE_FSTRING, string_node, sizeof(PyFStringNode));
    fstr->base.type = &TYPE_STRING;

    PyAstNode* prev = NULL;
    uint32_t child_count = ts_node_named_child_count(string_node);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(string_node, i);
        const char* child_type = ts_node_type(child);
        PyAstNode* part = NULL;

        if (strcmp(child_type, "string_content") == 0) {
            StrView content = py_node_source(tp, child);
            PyLiteralNode* lit = (PyLiteralNode*)alloc_py_ast_node(tp, PY_AST_NODE_LITERAL, child, sizeof(PyLiteralNode));
            lit->literal_type = PY_LITERAL_STRING;
            lit->value.string_value = name_pool_create_len(tp->name_pool, content.str, content.length);
            lit->base.type = &TYPE_STRING;
            part = (PyAstNode*)lit;
        } else if (strcmp(child_type, "interpolation") == 0) {
            TSNode expr_child = ts_node_child_by_field_name(child, "expression", 10);
            if (!ts_node_is_null(expr_child)) {
                PyAstNode* expr = build_py_expression(tp, expr_child);
                // check for format_specifier child (tree-sitter: "format_specifier" or "type_conversion")
                // tree-sitter-python interpolation children: { expression [! conversion] [: format_specifier] }
                String* fmt_spec = NULL;
                uint32_t interp_child_count = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < interp_child_count; j++) {
                    TSNode interp_child = ts_node_named_child(child, j);
                    const char* ic_type = ts_node_type(interp_child);
                    if (strcmp(ic_type, "format_specifier") == 0) {
                        // format_specifier contains the text after ':', strip the leading ':'
                        StrView spec_sv = py_node_source(tp, interp_child);
                        // tree-sitter includes the colon in format_specifier, skip it
                        if (spec_sv.length > 0 && spec_sv.str[0] == ':') {
                            fmt_spec = name_pool_create_len(tp->name_pool, spec_sv.str + 1, spec_sv.length - 1);
                        } else {
                            fmt_spec = name_pool_create_len(tp->name_pool, spec_sv.str, spec_sv.length);
                        }
                    }
                }
                if (fmt_spec && fmt_spec->len > 0) {
                    // wrap in PyFStringExprNode
                    PyFStringExprNode* fse = (PyFStringExprNode*)alloc_py_ast_node(
                        tp, PY_AST_NODE_FSTRING_EXPR, child, sizeof(PyFStringExprNode));
                    fse->expression = expr;
                    fse->format_spec = fmt_spec;
                    fse->base.type = &TYPE_STRING;
                    part = (PyAstNode*)fse;
                } else {
                    part = expr;
                }
            }
        }

        if (part) {
            if (!prev) {
                fstr->parts = part;
            } else {
                prev->next = part;
            }
            prev = part;
        }
    }

    return (PyAstNode*)fstr;
}

// ---- Expression builders ----

// Build binary operation
static PyAstNode* build_py_binary_op(PyTranspiler* tp, TSNode binary_node) {
    PyBinaryNode* binary = (PyBinaryNode*)alloc_py_ast_node(tp, PY_AST_NODE_BINARY_OP, binary_node, sizeof(PyBinaryNode));

    TSNode left_node = ts_node_child_by_field_name(binary_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(binary_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(binary_node, "operator", 8);

    binary->left = build_py_expression(tp, left_node);
    binary->right = build_py_expression(tp, right_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_source = py_node_source(tp, op_node);
        binary->op = py_operator_from_string(op_source.str, op_source.length);
    }

    binary->base.type = &TYPE_ANY;
    return (PyAstNode*)binary;
}

// Build boolean operation (and/or)
static PyAstNode* build_py_boolean_op(PyTranspiler* tp, TSNode bool_node) {
    PyBooleanNode* boolean = (PyBooleanNode*)alloc_py_ast_node(tp, PY_AST_NODE_BOOLEAN_OP, bool_node, sizeof(PyBooleanNode));

    TSNode left_node = ts_node_child_by_field_name(bool_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(bool_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(bool_node, "operator", 8);

    boolean->left = build_py_expression(tp, left_node);
    boolean->right = build_py_expression(tp, right_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_source = py_node_source(tp, op_node);
        boolean->op = py_operator_from_string(op_source.str, op_source.length);
    } else {
        boolean->op = PY_OP_AND;
    }

    boolean->base.type = &TYPE_BOOL;
    return (PyAstNode*)boolean;
}

// Build not operator
static PyAstNode* build_py_not_op(PyTranspiler* tp, TSNode not_node) {
    PyUnaryNode* unary = (PyUnaryNode*)alloc_py_ast_node(tp, PY_AST_NODE_NOT, not_node, sizeof(PyUnaryNode));

    TSNode arg_node = ts_node_child_by_field_name(not_node, "argument", 8);
    unary->operand = build_py_expression(tp, arg_node);
    unary->op = PY_OP_NOT;
    unary->base.type = &TYPE_BOOL;
    return (PyAstNode*)unary;
}

// Build unary operation (+, -, ~)
static PyAstNode* build_py_unary_op(PyTranspiler* tp, TSNode unary_node) {
    PyUnaryNode* unary = (PyUnaryNode*)alloc_py_ast_node(tp, PY_AST_NODE_UNARY_OP, unary_node, sizeof(PyUnaryNode));

    TSNode op_node = ts_node_child_by_field_name(unary_node, "operator", 8);
    TSNode arg_node = ts_node_child_by_field_name(unary_node, "argument", 8);

    unary->operand = build_py_expression(tp, arg_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_source = py_node_source(tp, op_node);
        if (op_source.length == 1 && op_source.str[0] == '-') {
            unary->op = PY_OP_NEGATE;
        } else if (op_source.length == 1 && op_source.str[0] == '+') {
            unary->op = PY_OP_POSITIVE;
        } else if (op_source.length == 1 && op_source.str[0] == '~') {
            unary->op = PY_OP_BIT_NOT;
        }
    }

    unary->base.type = &TYPE_ANY;
    return (PyAstNode*)unary;
}

// Build chained comparison: a < b < c
PyAstNode* build_py_comparison(PyTranspiler* tp, TSNode comp_node) {
    PyCompareNode* cmp = (PyCompareNode*)alloc_py_ast_node(tp, PY_AST_NODE_COMPARE, comp_node, sizeof(PyCompareNode));

    // comparison_operator has operands and operators as children
    // pattern: expr op expr op expr ...
    // unnamed children include operators as tokens

    uint32_t child_count = ts_node_child_count(comp_node);
    uint32_t named_count = ts_node_named_child_count(comp_node);

    // first named child is the leftmost operand
    if (named_count > 0) {
        cmp->left = build_py_expression(tp, ts_node_named_child(comp_node, 0));
    }

    // remaining named children are comparators
    int num_ops = named_count > 0 ? (int)named_count - 1 : 0;
    cmp->op_count = num_ops;

    if (num_ops > 0) {
        cmp->ops = (PyOperator*)pool_alloc(tp->ast_pool, sizeof(PyOperator) * num_ops);
        cmp->comparators = (PyAstNode**)pool_alloc(tp->ast_pool, sizeof(PyAstNode*) * num_ops);

        // build comparator expressions
        for (int i = 0; i < num_ops; i++) {
            cmp->comparators[i] = build_py_expression(tp, ts_node_named_child(comp_node, i + 1));
        }

        // extract operators from anonymous children
        int op_idx = 0;
        for (uint32_t i = 0; i < child_count && op_idx < num_ops; i++) {
            TSNode child = ts_node_child(comp_node, i);
            if (!ts_node_is_named(child)) {
                StrView op_src = py_node_source(tp, child);
                // skip parentheses and commas
                if (op_src.length > 0 && op_src.str[0] != '(' && op_src.str[0] != ')') {
                    // handle multi-word operators: "not in", "is not"
                    // check if this is "not" followed by "in"
                    if (op_src.length == 3 && strncmp(op_src.str, "not", 3) == 0) {
                        cmp->ops[op_idx++] = PY_OP_NOT_IN;
                    } else if (op_src.length == 2 && strncmp(op_src.str, "is", 2) == 0) {
                        // peek at next unnamed children for "not"
                        bool found_not = false;
                        for (uint32_t j = i + 1; j < child_count; j++) {
                            TSNode next = ts_node_child(comp_node, j);
                            if (ts_node_is_named(next)) break;
                            StrView next_src = py_node_source(tp, next);
                            if (next_src.length == 3 && strncmp(next_src.str, "not", 3) == 0) {
                                cmp->ops[op_idx++] = PY_OP_IS_NOT;
                                i = j; // skip "not" token
                                found_not = true;
                                break;
                            }
                        }
                        if (!found_not) {
                            cmp->ops[op_idx++] = PY_OP_IS;
                        }
                    } else if (op_src.length == 2 && strncmp(op_src.str, "in", 2) == 0) {
                        cmp->ops[op_idx++] = PY_OP_IN;
                    } else {
                        cmp->ops[op_idx++] = py_operator_from_string(op_src.str, op_src.length);
                    }
                }
            }
        }

        // if we didn't find enough operators from anonymous children, try named
        if (op_idx < num_ops) {
            for (int i = op_idx; i < num_ops; i++) {
                cmp->ops[i] = PY_OP_EQ; // fallback
            }
        }
    }

    cmp->base.type = &TYPE_BOOL;
    return (PyAstNode*)cmp;
}

// Build call expression
static PyAstNode* build_py_call(PyTranspiler* tp, TSNode call_node) {
    PyCallNode* call = (PyCallNode*)alloc_py_ast_node(tp, PY_AST_NODE_CALL, call_node, sizeof(PyCallNode));

    TSNode func_node = ts_node_child_by_field_name(call_node, "function", 8);
    TSNode args_node = ts_node_child_by_field_name(call_node, "arguments", 9);

    call->function = build_py_expression(tp, func_node);
    call->arg_count = 0;

    // build arguments list
    if (!ts_node_is_null(args_node)) {
        uint32_t arg_count = ts_node_named_child_count(args_node);
        PyAstNode* prev = NULL;

        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg = ts_node_named_child(args_node, i);
            const char* arg_type = ts_node_type(arg);
            PyAstNode* arg_node = NULL;

            if (strcmp(arg_type, "keyword_argument") == 0) {
                PyKeywordArgNode* kw = (PyKeywordArgNode*)alloc_py_ast_node(tp, PY_AST_NODE_KEYWORD_ARGUMENT, arg, sizeof(PyKeywordArgNode));
                TSNode kw_name = ts_node_child_by_field_name(arg, "name", 4);
                TSNode kw_value = ts_node_child_by_field_name(arg, "value", 5);

                if (!ts_node_is_null(kw_name)) {
                    StrView name_src = py_node_source(tp, kw_name);
                    kw->key = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                }
                kw->value = build_py_expression(tp, kw_value);
                kw->base.type = &TYPE_ANY;
                arg_node = (PyAstNode*)kw;
            } else if (strcmp(arg_type, "list_splat") == 0) {
                // *args
                PyStarredNode* starred = (PyStarredNode*)alloc_py_ast_node(tp, PY_AST_NODE_STARRED, arg, sizeof(PyStarredNode));
                if (ts_node_named_child_count(arg) > 0) {
                    starred->value = build_py_expression(tp, ts_node_named_child(arg, 0));
                }
                starred->base.type = &TYPE_ANY;
                arg_node = (PyAstNode*)starred;
            } else if (strcmp(arg_type, "dictionary_splat") == 0) {
                // **kwargs
                PyKeywordArgNode* kw = (PyKeywordArgNode*)alloc_py_ast_node(tp, PY_AST_NODE_KEYWORD_ARGUMENT, arg, sizeof(PyKeywordArgNode));
                kw->key = NULL; // NULL key indicates **kwargs
                if (ts_node_named_child_count(arg) > 0) {
                    kw->value = build_py_expression(tp, ts_node_named_child(arg, 0));
                }
                kw->base.type = &TYPE_ANY;
                arg_node = (PyAstNode*)kw;
            } else {
                arg_node = build_py_expression(tp, arg);
            }

            if (arg_node) {
                call->arg_count++;
                if (!prev) {
                    call->arguments = arg_node;
                } else {
                    prev->next = arg_node;
                }
                prev = arg_node;
            }
        }
    }

    call->base.type = &TYPE_ANY;
    return (PyAstNode*)call;
}

// Build attribute access (obj.attr)
static PyAstNode* build_py_attribute(PyTranspiler* tp, TSNode attr_node) {
    PyAttributeNode* attr = (PyAttributeNode*)alloc_py_ast_node(tp, PY_AST_NODE_ATTRIBUTE, attr_node, sizeof(PyAttributeNode));

    TSNode object_node = ts_node_child_by_field_name(attr_node, "object", 6);
    TSNode attr_name_node = ts_node_child_by_field_name(attr_node, "attribute", 9);

    attr->object = build_py_expression(tp, object_node);

    if (!ts_node_is_null(attr_name_node)) {
        StrView name_src = py_node_source(tp, attr_name_node);
        attr->attribute = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
    }

    attr->base.type = &TYPE_ANY;
    return (PyAstNode*)attr;
}

// Build subscript expression (obj[key])
static PyAstNode* build_py_subscript(PyTranspiler* tp, TSNode sub_node) {
    PySubscriptNode* sub = (PySubscriptNode*)alloc_py_ast_node(tp, PY_AST_NODE_SUBSCRIPT, sub_node, sizeof(PySubscriptNode));

    TSNode value_node = ts_node_child_by_field_name(sub_node, "value", 5);
    TSNode subscript_node = ts_node_child_by_field_name(sub_node, "subscript", 9);

    sub->object = build_py_expression(tp, value_node);

    if (!ts_node_is_null(subscript_node)) {
        const char* sub_type = ts_node_type(subscript_node);
        if (strcmp(sub_type, "slice") == 0) {
            sub->index = build_py_slice(tp, subscript_node);
        } else {
            sub->index = build_py_expression(tp, subscript_node);
        }
    }

    sub->base.type = &TYPE_ANY;
    return (PyAstNode*)sub;
}

// Build slice expression (start:stop:step)
PyAstNode* build_py_slice(PyTranspiler* tp, TSNode slice_node) {
    PySliceNode* slice = (PySliceNode*)alloc_py_ast_node(tp, PY_AST_NODE_SLICE, slice_node, sizeof(PySliceNode));

    // slice children: up to 3 expressions separated by colons
    // track colon count to determine which slot (start/stop/step) each expression fills
    uint32_t child_count = ts_node_child_count(slice_node);
    int colon_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(slice_node, i);
        if (ts_node_is_named(child)) {
            PyAstNode* expr = build_py_expression(tp, child);
            switch (colon_count) {
                case 0: slice->start = expr; break;
                case 1: slice->stop = expr; break;
                case 2: slice->step = expr; break;
            }
        } else {
            StrView src = py_node_source(tp, child);
            if (src.length == 1 && src.str[0] == ':') {
                colon_count++;
            }
        }
    }

    slice->base.type = &TYPE_ANY;
    return (PyAstNode*)slice;
}

// Build sequence node (list, tuple, set)
static PyAstNode* build_py_sequence(PyTranspiler* tp, TSNode seq_node, PyAstNodeType type) {
    PySequenceNode* seq = (PySequenceNode*)alloc_py_ast_node(tp, type, seq_node, sizeof(PySequenceNode));

    uint32_t child_count = ts_node_named_child_count(seq_node);
    seq->length = 0;
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(seq_node, i);
        const char* child_type = ts_node_type(child);
        PyAstNode* elem = NULL;

        if (strcmp(child_type, "list_splat") == 0) {
            PyStarredNode* starred = (PyStarredNode*)alloc_py_ast_node(tp, PY_AST_NODE_STARRED, child, sizeof(PyStarredNode));
            if (ts_node_named_child_count(child) > 0) {
                starred->value = build_py_expression(tp, ts_node_named_child(child, 0));
            }
            starred->base.type = &TYPE_ANY;
            elem = (PyAstNode*)starred;
        } else {
            elem = build_py_expression(tp, child);
        }

        if (elem) {
            seq->length++;
            if (!prev) {
                seq->elements = elem;
            } else {
                prev->next = elem;
            }
            prev = elem;
        }
    }

    if (type == PY_AST_NODE_LIST) {
        seq->base.type = &TYPE_LIST;
    } else if (type == PY_AST_NODE_TUPLE) {
        seq->base.type = &TYPE_LIST; // tuples map to array
    } else {
        seq->base.type = &TYPE_LIST; // sets also map to array for now
    }

    return (PyAstNode*)seq;
}

// Build dictionary literal
PyAstNode* build_py_dict(PyTranspiler* tp, TSNode dict_node) {
    PyDictNode* dict = (PyDictNode*)alloc_py_ast_node(tp, PY_AST_NODE_DICT, dict_node, sizeof(PyDictNode));

    uint32_t child_count = ts_node_named_child_count(dict_node);
    dict->length = 0;
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(dict_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "pair") == 0) {
            PyPairNode* pair = (PyPairNode*)alloc_py_ast_node(tp, PY_AST_NODE_PAIR, child, sizeof(PyPairNode));

            TSNode key_node = ts_node_child_by_field_name(child, "key", 3);
            TSNode value_node = ts_node_child_by_field_name(child, "value", 5);

            pair->key = build_py_expression(tp, key_node);
            pair->value = build_py_expression(tp, value_node);
            pair->base.type = &TYPE_ANY;

            dict->length++;
            if (!prev) {
                dict->pairs = (PyAstNode*)pair;
            } else {
                prev->next = (PyAstNode*)pair;
            }
            prev = (PyAstNode*)pair;
        } else if (strcmp(child_type, "dictionary_splat") == 0) {
            // **kwargs in dict literal
            PyStarredNode* starred = (PyStarredNode*)alloc_py_ast_node(tp, PY_AST_NODE_STARRED, child, sizeof(PyStarredNode));
            if (ts_node_named_child_count(child) > 0) {
                starred->value = build_py_expression(tp, ts_node_named_child(child, 0));
            }
            starred->base.type = &TYPE_ANY;

            dict->length++;
            if (!prev) {
                dict->pairs = (PyAstNode*)starred;
            } else {
                prev->next = (PyAstNode*)starred;
            }
            prev = (PyAstNode*)starred;
        }
    }

    dict->base.type = &TYPE_MAP;
    return (PyAstNode*)dict;
}

// Build comprehension (list, dict, set, generator)
PyAstNode* build_py_list_comprehension(PyTranspiler* tp, TSNode comp_node, PyAstNodeType comp_type) {
    PyComprehensionNode* comp = (PyComprehensionNode*)alloc_py_ast_node(tp, comp_type, comp_node, sizeof(PyComprehensionNode));

    // body field contains the output expression
    TSNode body_node = ts_node_child_by_field_name(comp_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        comp->element = build_py_expression(tp, body_node);
    }

    // iterate named children for for_in_clause and if_clause
    uint32_t child_count = ts_node_named_child_count(comp_node);
    PyComprehensionNode* current_comp = comp;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(comp_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "for_in_clause") == 0) {
            TSNode left = ts_node_child_by_field_name(child, "left", 4);
            TSNode right = ts_node_child_by_field_name(child, "right", 5);

            if (current_comp == comp) {
                // first for clause
                current_comp->target = build_py_expression(tp, left);
                current_comp->iter = build_py_expression(tp, right);
            } else {
                // nested for clause
                PyComprehensionNode* inner = (PyComprehensionNode*)alloc_py_ast_node(tp, comp_type, child, sizeof(PyComprehensionNode));
                inner->target = build_py_expression(tp, left);
                inner->iter = build_py_expression(tp, right);
                current_comp->inner = (PyAstNode*)inner;
                current_comp = inner;
            }
        } else if (strcmp(child_type, "if_clause") == 0) {
            if (ts_node_named_child_count(child) > 0) {
                PyAstNode* cond = build_py_expression(tp, ts_node_named_child(child, 0));
                // add to current comp's conditions linked list
                if (!current_comp->conditions) {
                    current_comp->conditions = cond;
                } else {
                    PyAstNode* last = current_comp->conditions;
                    while (last->next) last = last->next;
                    last->next = cond;
                }
            }
        }
    }

    if (comp_type == PY_AST_NODE_DICT_COMPREHENSION) {
        comp->base.type = &TYPE_MAP;
    } else {
        comp->base.type = &TYPE_LIST;
    }

    return (PyAstNode*)comp;
}

// Build lambda expression
PyAstNode* build_py_lambda(PyTranspiler* tp, TSNode lambda_node) {
    PyLambdaNode* lambda = (PyLambdaNode*)alloc_py_ast_node(tp, PY_AST_NODE_LAMBDA, lambda_node, sizeof(PyLambdaNode));

    TSNode params_node = ts_node_child_by_field_name(lambda_node, "parameters", 10);
    TSNode body_node = ts_node_child_by_field_name(lambda_node, "body", 4);

    if (!ts_node_is_null(params_node)) {
        lambda->params = build_py_parameters(tp, params_node);
    }
    if (!ts_node_is_null(body_node)) {
        lambda->body = build_py_expression(tp, body_node);
    }

    lambda->base.type = &TYPE_FUNC;
    return (PyAstNode*)lambda;
}

// Build conditional expression (x if cond else y)
PyAstNode* build_py_conditional_expr(PyTranspiler* tp, TSNode cond_node) {
    PyConditionalNode* cond = (PyConditionalNode*)alloc_py_ast_node(tp, PY_AST_NODE_CONDITIONAL_EXPR, cond_node, sizeof(PyConditionalNode));

    // conditional_expression: 3 unnamed children — value, condition, alternative
    uint32_t named_count = ts_node_named_child_count(cond_node);
    if (named_count >= 3) {
        cond->body = build_py_expression(tp, ts_node_named_child(cond_node, 0));
        cond->test = build_py_expression(tp, ts_node_named_child(cond_node, 1));
        cond->else_body = build_py_expression(tp, ts_node_named_child(cond_node, 2));
    }

    cond->base.type = &TYPE_ANY;
    return (PyAstNode*)cond;
}

// ---- Statement builders ----

// Build assignment statement
static PyAstNode* build_py_assignment(PyTranspiler* tp, TSNode assign_node) {
    PyAssignmentNode* assign = (PyAssignmentNode*)alloc_py_ast_node(tp, PY_AST_NODE_ASSIGNMENT, assign_node, sizeof(PyAssignmentNode));

    TSNode left_node = ts_node_child_by_field_name(assign_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(assign_node, "right", 5);

    assign->targets = build_py_expression(tp, left_node);
    if (!ts_node_is_null(right_node)) {
        assign->value = build_py_expression(tp, right_node);
    }

    // register variable in scope
    if (assign->targets && assign->targets->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)assign->targets;
        py_scope_define(tp, id->name, assign->targets, PY_VAR_LOCAL);
    }

    assign->base.type = &TYPE_ANY;
    return (PyAstNode*)assign;
}

// Build augmented assignment (+=, -=, etc.)
static PyAstNode* build_py_augmented_assignment(PyTranspiler* tp, TSNode aug_node) {
    PyAugAssignmentNode* aug = (PyAugAssignmentNode*)alloc_py_ast_node(tp, PY_AST_NODE_AUGMENTED_ASSIGNMENT, aug_node, sizeof(PyAugAssignmentNode));

    TSNode left_node = ts_node_child_by_field_name(aug_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(aug_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(aug_node, "operator", 8);

    aug->target = build_py_expression(tp, left_node);
    aug->value = build_py_expression(tp, right_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_source = py_node_source(tp, op_node);
        aug->op = py_augmented_operator_from_string(op_source.str, op_source.length);
    }

    aug->base.type = &TYPE_ANY;
    return (PyAstNode*)aug;
}

// Build return statement
static PyAstNode* build_py_return(PyTranspiler* tp, TSNode return_node) {
    PyReturnNode* ret = (PyReturnNode*)alloc_py_ast_node(tp, PY_AST_NODE_RETURN, return_node, sizeof(PyReturnNode));

    // return_statement has no named fields; children are expressions
    uint32_t child_count = ts_node_named_child_count(return_node);
    if (child_count > 0) {
        ret->value = build_py_expression(tp, ts_node_named_child(return_node, 0));
    }

    ret->base.type = &TYPE_ANY;
    return (PyAstNode*)ret;
}

// Build block (indented suite of statements)
PyAstNode* build_py_block(PyTranspiler* tp, TSNode block_node) {
    PyBlockNode* block = (PyBlockNode*)alloc_py_ast_node(tp, PY_AST_NODE_BLOCK, block_node, sizeof(PyBlockNode));

    uint32_t child_count = ts_node_named_child_count(block_node);
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(block_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "comment") == 0) continue;

        PyAstNode* stmt = build_py_statement(tp, child);
        if (stmt) {
            if (!prev) {
                block->statements = stmt;
            } else {
                prev->next = stmt;
            }
            prev = stmt;
        }
    }

    return (PyAstNode*)block;
}

// Build if statement
PyAstNode* build_py_if_statement(PyTranspiler* tp, TSNode if_node) {
    PyIfNode* if_stmt = (PyIfNode*)alloc_py_ast_node(tp, PY_AST_NODE_IF, if_node, sizeof(PyIfNode));

    TSNode cond_node = ts_node_child_by_field_name(if_node, "condition", 9);
    TSNode body_node = ts_node_child_by_field_name(if_node, "consequence", 11);

    if_stmt->test = build_py_expression(tp, cond_node);
    if (!ts_node_is_null(body_node)) {
        if_stmt->body = build_py_block(tp, body_node);
    }

    // handle elif and else clauses via the "alternative" field
    uint32_t child_count = ts_node_named_child_count(if_node);
    PyAstNode* prev_elif = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(if_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "elif_clause") == 0) {
            PyIfNode* elif = (PyIfNode*)alloc_py_ast_node(tp, PY_AST_NODE_ELIF, child, sizeof(PyIfNode));
            TSNode elif_cond = ts_node_child_by_field_name(child, "condition", 9);
            TSNode elif_body = ts_node_child_by_field_name(child, "consequence", 11);

            elif->test = build_py_expression(tp, elif_cond);
            if (!ts_node_is_null(elif_body)) {
                elif->body = build_py_block(tp, elif_body);
            }

            if (!prev_elif) {
                if_stmt->elif_clauses = (PyAstNode*)elif;
            } else {
                prev_elif->next = (PyAstNode*)elif;
            }
            prev_elif = (PyAstNode*)elif;
        } else if (strcmp(child_type, "else_clause") == 0) {
            TSNode else_body = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(else_body)) {
                if_stmt->else_body = build_py_block(tp, else_body);
            }
        }
    }

    if_stmt->base.type = &TYPE_ANY;
    return (PyAstNode*)if_stmt;
}

// Build while statement
PyAstNode* build_py_while_statement(PyTranspiler* tp, TSNode while_node) {
    PyWhileNode* while_stmt = (PyWhileNode*)alloc_py_ast_node(tp, PY_AST_NODE_WHILE, while_node, sizeof(PyWhileNode));

    TSNode cond_node = ts_node_child_by_field_name(while_node, "condition", 9);
    TSNode body_node = ts_node_child_by_field_name(while_node, "body", 4);

    while_stmt->test = build_py_expression(tp, cond_node);
    if (!ts_node_is_null(body_node)) {
        while_stmt->body = build_py_block(tp, body_node);
    }

    while_stmt->base.type = &TYPE_ANY;
    return (PyAstNode*)while_stmt;
}

// Build for statement
PyAstNode* build_py_for_statement(PyTranspiler* tp, TSNode for_node) {
    PyForNode* for_stmt = (PyForNode*)alloc_py_ast_node(tp, PY_AST_NODE_FOR, for_node, sizeof(PyForNode));

    TSNode left_node = ts_node_child_by_field_name(for_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(for_node, "right", 5);
    TSNode body_node = ts_node_child_by_field_name(for_node, "body", 4);

    for_stmt->target = build_py_expression(tp, left_node);
    for_stmt->iter = build_py_expression(tp, right_node);
    if (!ts_node_is_null(body_node)) {
        for_stmt->body = build_py_block(tp, body_node);
    }

    // register loop variable in scope
    if (for_stmt->target && for_stmt->target->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)for_stmt->target;
        py_scope_define(tp, id->name, for_stmt->target, PY_VAR_LOCAL);
    }

    for_stmt->base.type = &TYPE_ANY;
    return (PyAstNode*)for_stmt;
}

// Build function definition
PyAstNode* build_py_function_def(PyTranspiler* tp, TSNode func_node) {
    PyFunctionDefNode* func = (PyFunctionDefNode*)alloc_py_ast_node(tp, PY_AST_NODE_FUNCTION_DEF, func_node, sizeof(PyFunctionDefNode));

    TSNode name_node = ts_node_child_by_field_name(func_node, "name", 4);
    TSNode params_node = ts_node_child_by_field_name(func_node, "parameters", 10);
    TSNode body_node = ts_node_child_by_field_name(func_node, "body", 4);
    TSNode ret_type_node = ts_node_child_by_field_name(func_node, "return_type", 11);

    // name
    if (!ts_node_is_null(name_node)) {
        StrView name_src = py_node_source(tp, name_node);
        func->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);

        // register function in current scope
        py_scope_define(tp, func->name, (PyAstNode*)func, PY_VAR_LOCAL);
    }

    // create a new function scope
    PyScope* func_scope = py_scope_create(tp, PY_SCOPE_FUNCTION, tp->current_scope);
    func_scope->function = func;
    py_scope_push(tp, func_scope);

    // parameters
    if (!ts_node_is_null(params_node)) {
        func->params = build_py_parameters(tp, params_node);
    }

    // body
    if (!ts_node_is_null(body_node)) {
        func->body = build_py_block(tp, body_node);
    }

    // return annotation (stored but not enforced)
    if (!ts_node_is_null(ret_type_node)) {
        func->return_annotation = build_py_expression(tp, ret_type_node);
    }

    py_scope_pop(tp);

    func->base.type = &TYPE_FUNC;
    return (PyAstNode*)func;
}

// Build function parameters
PyAstNode* build_py_parameters(PyTranspiler* tp, TSNode params_node) {
    uint32_t child_count = ts_node_named_child_count(params_node);
    PyAstNode* first = NULL;
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(params_node, i);
        const char* child_type = ts_node_type(child);
        PyAstNode* param = NULL;

        if (strcmp(child_type, "identifier") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_PARAMETER, child, sizeof(PyParamNode));
            StrView name_src = py_node_source(tp, child);
            p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
            p->base.type = &TYPE_ANY;
            // register parameter in scope
            py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "default_parameter") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_DEFAULT_PARAMETER, child, sizeof(PyParamNode));
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            TSNode value_node = ts_node_child_by_field_name(child, "value", 5);
            if (!ts_node_is_null(name_node)) {
                StrView name_src = py_node_source(tp, name_node);
                p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
            }
            if (!ts_node_is_null(value_node)) {
                p->default_value = build_py_expression(tp, value_node);
            }
            p->base.type = &TYPE_ANY;
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "typed_parameter") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_TYPED_PARAMETER, child, sizeof(PyParamNode));
            // first named child is the identifier (or splat pattern), then type
            if (ts_node_named_child_count(child) > 0) {
                TSNode param_child = ts_node_named_child(child, 0);
                if (strcmp(ts_node_type(param_child), "identifier") == 0) {
                    StrView name_src = py_node_source(tp, param_child);
                    p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                    py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
                }
            }
            TSNode type_node = ts_node_child_by_field_name(child, "type", 4);
            if (!ts_node_is_null(type_node)) {
                p->annotation = build_py_expression(tp, type_node);
            }
            p->base.type = &TYPE_ANY;
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "typed_default_parameter") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_DEFAULT_PARAMETER, child, sizeof(PyParamNode));
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            TSNode value_node = ts_node_child_by_field_name(child, "value", 5);
            if (!ts_node_is_null(name_node)) {
                StrView name_src = py_node_source(tp, name_node);
                p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
            }
            if (!ts_node_is_null(value_node)) {
                p->default_value = build_py_expression(tp, value_node);
            }
            p->base.type = &TYPE_ANY;
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "list_splat_pattern") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_LIST_SPLAT_PARAMETER, child, sizeof(PyParamNode));
            if (ts_node_named_child_count(child) > 0) {
                TSNode name_child = ts_node_named_child(child, 0);
                StrView name_src = py_node_source(tp, name_child);
                p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
            }
            p->base.type = &TYPE_ANY;
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "dictionary_splat_pattern") == 0) {
            PyParamNode* p = (PyParamNode*)alloc_py_ast_node(tp, PY_AST_NODE_DICT_SPLAT_PARAMETER, child, sizeof(PyParamNode));
            if (ts_node_named_child_count(child) > 0) {
                TSNode name_child = ts_node_named_child(child, 0);
                StrView name_src = py_node_source(tp, name_child);
                p->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                py_scope_define(tp, p->name, (PyAstNode*)p, PY_VAR_LOCAL);
            }
            p->base.type = &TYPE_ANY;
            param = (PyAstNode*)p;
        } else if (strcmp(child_type, "keyword_separator") == 0 || strcmp(child_type, "positional_separator") == 0) {
            continue; // skip bare * and / separators
        }

        if (param) {
            if (!prev) {
                first = param;
            } else {
                prev->next = param;
            }
            prev = param;
        }
    }

    return first;
}

// Build class definition
PyAstNode* build_py_class_def(PyTranspiler* tp, TSNode class_node) {
    PyClassDefNode* cls = (PyClassDefNode*)alloc_py_ast_node(tp, PY_AST_NODE_CLASS_DEF, class_node, sizeof(PyClassDefNode));

    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    TSNode bases_node = ts_node_child_by_field_name(class_node, "superclasses", 12);
    TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);

    if (!ts_node_is_null(name_node)) {
        StrView name_src = py_node_source(tp, name_node);
        cls->name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
        py_scope_define(tp, cls->name, (PyAstNode*)cls, PY_VAR_LOCAL);
    }

    // base classes
    if (!ts_node_is_null(bases_node)) {
        uint32_t base_count = ts_node_named_child_count(bases_node);
        PyAstNode* prev = NULL;
        for (uint32_t i = 0; i < base_count; i++) {
            PyAstNode* base = build_py_expression(tp, ts_node_named_child(bases_node, i));
            if (base) {
                if (!prev) {
                    cls->bases = base;
                } else {
                    prev->next = base;
                }
                prev = base;
            }
        }
    }

    // class scope and body
    PyScope* class_scope = py_scope_create(tp, PY_SCOPE_CLASS, tp->current_scope);
    py_scope_push(tp, class_scope);

    if (!ts_node_is_null(body_node)) {
        cls->body = build_py_block(tp, body_node);
    }

    py_scope_pop(tp);

    cls->base.type = &TYPE_MAP;
    return (PyAstNode*)cls;
}

// Build try statement
PyAstNode* build_py_try_statement(PyTranspiler* tp, TSNode try_node) {
    PyTryNode* try_stmt = (PyTryNode*)alloc_py_ast_node(tp, PY_AST_NODE_TRY, try_node, sizeof(PyTryNode));

    TSNode body_node = ts_node_child_by_field_name(try_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        try_stmt->body = build_py_block(tp, body_node);
    }

    // iterate children for except, else, finally clauses
    uint32_t child_count = ts_node_named_child_count(try_node);
    PyAstNode* prev_handler = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(try_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "except_clause") == 0) {
            PyExceptNode* handler = (PyExceptNode*)alloc_py_ast_node(tp, PY_AST_NODE_EXCEPT, child, sizeof(PyExceptNode));

            // exception type (optional)
            uint32_t exc_named = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < exc_named; j++) {
                TSNode exc_child = ts_node_named_child(child, j);
                const char* exc_type = ts_node_type(exc_child);
                if (strcmp(exc_type, "block") == 0) {
                    handler->body = build_py_block(tp, exc_child);
                } else if (strcmp(exc_type, "identifier") == 0 || strcmp(exc_type, "attribute") == 0) {
                    // could be exception type or alias
                    TSNode alias_node = ts_node_child_by_field_name(child, "alias", 5);
                    if (!ts_node_is_null(alias_node) && ts_node_start_byte(alias_node) == ts_node_start_byte(exc_child)) {
                        StrView alias_src = py_node_source(tp, alias_node);
                        handler->name = name_pool_create_len(tp->name_pool, alias_src.str, alias_src.length);
                    } else if (!handler->type) {
                        handler->type = build_py_expression(tp, exc_child);
                    } else {
                        // this is the alias
                        StrView alias_src = py_node_source(tp, exc_child);
                        handler->name = name_pool_create_len(tp->name_pool, alias_src.str, alias_src.length);
                    }
                }
            }

            handler->base.type = &TYPE_ANY;
            if (!prev_handler) {
                try_stmt->handlers = (PyAstNode*)handler;
            } else {
                prev_handler->next = (PyAstNode*)handler;
            }
            prev_handler = (PyAstNode*)handler;
        } else if (strcmp(child_type, "else_clause") == 0) {
            TSNode else_body = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(else_body)) {
                try_stmt->else_body = build_py_block(tp, else_body);
            }
        } else if (strcmp(child_type, "finally_clause") == 0) {
            // finally clause has a block child
            uint32_t fin_count = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < fin_count; j++) {
                TSNode fin_child = ts_node_named_child(child, j);
                if (strcmp(ts_node_type(fin_child), "block") == 0) {
                    try_stmt->finally_body = build_py_block(tp, fin_child);
                    break;
                }
            }
        }
    }

    try_stmt->base.type = &TYPE_ANY;
    return (PyAstNode*)try_stmt;
}

// Build raise statement
static PyAstNode* build_py_raise(PyTranspiler* tp, TSNode raise_node) {
    PyRaiseNode* raise = (PyRaiseNode*)alloc_py_ast_node(tp, PY_AST_NODE_RAISE, raise_node, sizeof(PyRaiseNode));

    uint32_t child_count = ts_node_named_child_count(raise_node);
    if (child_count > 0) {
        raise->exception = build_py_expression(tp, ts_node_named_child(raise_node, 0));
    }

    raise->base.type = &TYPE_ANY;
    return (PyAstNode*)raise;
}

// Build assert statement
static PyAstNode* build_py_assert(PyTranspiler* tp, TSNode assert_node) {
    PyAssertNode* assert_stmt = (PyAssertNode*)alloc_py_ast_node(tp, PY_AST_NODE_ASSERT, assert_node, sizeof(PyAssertNode));

    uint32_t child_count = ts_node_named_child_count(assert_node);
    if (child_count > 0) {
        assert_stmt->test = build_py_expression(tp, ts_node_named_child(assert_node, 0));
    }
    if (child_count > 1) {
        assert_stmt->message = build_py_expression(tp, ts_node_named_child(assert_node, 1));
    }

    assert_stmt->base.type = &TYPE_ANY;
    return (PyAstNode*)assert_stmt;
}

// Build with statement
PyAstNode* build_py_with_statement(PyTranspiler* tp, TSNode with_node) {
    PyWithNode* with = (PyWithNode*)alloc_py_ast_node(tp, PY_AST_NODE_WITH, with_node, sizeof(PyWithNode));

    TSNode body_node = ts_node_child_by_field_name(with_node, "body", 4);

    // find the with_clause child
    uint32_t child_count = ts_node_named_child_count(with_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(with_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "with_clause") == 0) {
            // get the with_item inside
            uint32_t clause_count = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < clause_count; j++) {
                TSNode item = ts_node_named_child(child, j);
                if (strcmp(ts_node_type(item), "with_item") == 0) {
                    TSNode value = ts_node_child_by_field_name(item, "value", 5);
                    if (!ts_node_is_null(value)) {
                        // in tree-sitter-python, `with expr as target:` encodes the `as target`
                        // inside the value as an `as_pattern` node (alias field), not as a
                        // separate with_item field. Unwrap it here.
                        if (strcmp(ts_node_type(value), "as_pattern") == 0) {
                            // as_pattern has: expression (unnamed) + field('alias', as_pattern_target)
                            // the context manager expression is the first named child
                            TSNode mgr_expr = ts_node_named_child(value, 0);
                            if (!ts_node_is_null(mgr_expr)) {
                                with->items = build_py_expression(tp, mgr_expr);
                            }
                            TSNode alias_node = ts_node_child_by_field_name(value, "alias", 5);
                            if (!ts_node_is_null(alias_node)) {
                                StrView alias_src = py_node_source(tp, alias_node);
                                with->target = name_pool_create_len(tp->name_pool, alias_src.str, alias_src.length);
                            }
                        } else {
                            with->items = build_py_expression(tp, value);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (!ts_node_is_null(body_node)) {
        with->body = build_py_block(tp, body_node);
    }

    with->base.type = &TYPE_ANY;
    return (PyAstNode*)with;
}

// Build global/nonlocal statement
static PyAstNode* build_py_global_nonlocal(PyTranspiler* tp, TSNode gn_node, PyAstNodeType type) {
    PyGlobalNonlocalNode* gn = (PyGlobalNonlocalNode*)alloc_py_ast_node(tp, type, gn_node, sizeof(PyGlobalNonlocalNode));

    uint32_t child_count = ts_node_named_child_count(gn_node);
    gn->name_count = child_count;

    if (child_count > 0) {
        gn->names = (String**)pool_alloc(tp->ast_pool, sizeof(String*) * child_count);

        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(gn_node, i);
            StrView name_src = py_node_source(tp, child);
            gn->names[i] = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);

            // register variable with the appropriate kind
            PyVarKind kind = (type == PY_AST_NODE_GLOBAL) ? PY_VAR_GLOBAL : PY_VAR_NONLOCAL;
            py_scope_define(tp, gn->names[i], (PyAstNode*)gn, kind);
        }
    }

    gn->base.type = &TYPE_ANY;
    return (PyAstNode*)gn;
}

// Build del statement
static PyAstNode* build_py_del(PyTranspiler* tp, TSNode del_node) {
    PyDelNode* del = (PyDelNode*)alloc_py_ast_node(tp, PY_AST_NODE_DEL, del_node, sizeof(PyDelNode));

    uint32_t child_count = ts_node_named_child_count(del_node);
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        PyAstNode* target = build_py_expression(tp, ts_node_named_child(del_node, i));
        if (target) {
            if (!prev) {
                del->targets = target;
            } else {
                prev->next = target;
            }
            prev = target;
        }
    }

    del->base.type = &TYPE_ANY;
    return (PyAstNode*)del;
}

// Build import statement
static PyAstNode* build_py_import(PyTranspiler* tp, TSNode import_node) {
    PyImportNode* imp = (PyImportNode*)alloc_py_ast_node(tp, PY_AST_NODE_IMPORT, import_node, sizeof(PyImportNode));

    uint32_t child_count = ts_node_named_child_count(import_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(import_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "dotted_name") == 0) {
            StrView name_src = py_node_source(tp, child);
            imp->module_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
        } else if (strcmp(child_type, "aliased_import") == 0) {
            TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
            TSNode alias_n = ts_node_child_by_field_name(child, "alias", 5);
            if (!ts_node_is_null(name_n)) {
                StrView name_src = py_node_source(tp, name_n);
                imp->module_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
            }
            if (!ts_node_is_null(alias_n)) {
                StrView alias_src = py_node_source(tp, alias_n);
                imp->alias = name_pool_create_len(tp->name_pool, alias_src.str, alias_src.length);
            }
        }
    }

    imp->base.type = &TYPE_ANY;
    return (PyAstNode*)imp;
}

// Build from...import statement
static PyAstNode* build_py_import_from(PyTranspiler* tp, TSNode import_node) {
    PyImportNode* imp = (PyImportNode*)alloc_py_ast_node(tp, PY_AST_NODE_IMPORT_FROM, import_node, sizeof(PyImportNode));

    TSNode module_node = ts_node_child_by_field_name(import_node, "module_name", 11);
    if (!ts_node_is_null(module_node)) {
        StrView name_src = py_node_source(tp, module_node);
        imp->module_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
    }

    // imported names
    uint32_t child_count = ts_node_named_child_count(import_node);
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(import_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "dotted_name") == 0 || strcmp(child_type, "aliased_import") == 0) {
            // skip the module_name node itself (it appears as a dotted_name child too)
            if (!ts_node_is_null(module_node) && ts_node_eq(child, module_node)) continue;

            PyImportNode* name_imp = (PyImportNode*)alloc_py_ast_node(tp, PY_AST_NODE_IMPORT, child, sizeof(PyImportNode));

            if (strcmp(child_type, "aliased_import") == 0) {
                TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
                TSNode alias_n = ts_node_child_by_field_name(child, "alias", 5);
                if (!ts_node_is_null(name_n)) {
                    StrView name_src = py_node_source(tp, name_n);
                    name_imp->module_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
                }
                if (!ts_node_is_null(alias_n)) {
                    StrView alias_src = py_node_source(tp, alias_n);
                    name_imp->alias = name_pool_create_len(tp->name_pool, alias_src.str, alias_src.length);
                }
            } else {
                StrView name_src = py_node_source(tp, child);
                name_imp->module_name = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
            }

            name_imp->base.type = &TYPE_ANY;
            if (!prev) {
                imp->names = (PyAstNode*)name_imp;
            } else {
                prev->next = (PyAstNode*)name_imp;
            }
            prev = (PyAstNode*)name_imp;
        } else if (strcmp(child_type, "wildcard_import") == 0) {
            // from module import *
            PyImportNode* star_imp = (PyImportNode*)alloc_py_ast_node(tp, PY_AST_NODE_IMPORT, child, sizeof(PyImportNode));
            star_imp->module_name = name_pool_create_len(tp->name_pool, "*", 1);
            star_imp->base.type = &TYPE_ANY;
            if (!prev) {
                imp->names = (PyAstNode*)star_imp;
            } else {
                prev->next = (PyAstNode*)star_imp;
            }
            prev = (PyAstNode*)star_imp;
        }
    }

    imp->base.type = &TYPE_ANY;
    return (PyAstNode*)imp;
}

// Build decorated definition
PyAstNode* build_py_decorated_definition(PyTranspiler* tp, TSNode dec_node) {
    TSNode def_node = ts_node_child_by_field_name(dec_node, "definition", 10);

    // build decorators first
    PyAstNode* decorators = NULL;
    PyAstNode* prev_dec = NULL;
    uint32_t child_count = ts_node_named_child_count(dec_node);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(dec_node, i);
        if (strcmp(ts_node_type(child), "decorator") == 0) {
            PyDecoratorNode* decorator = (PyDecoratorNode*)alloc_py_ast_node(tp, PY_AST_NODE_DECORATOR, child, sizeof(PyDecoratorNode));
            if (ts_node_named_child_count(child) > 0) {
                decorator->expression = build_py_expression(tp, ts_node_named_child(child, 0));
            }
            decorator->base.type = &TYPE_ANY;

            if (!prev_dec) {
                decorators = (PyAstNode*)decorator;
            } else {
                prev_dec->next = (PyAstNode*)decorator;
            }
            prev_dec = (PyAstNode*)decorator;
        }
    }

    // build the actual definition
    if (!ts_node_is_null(def_node)) {
        const char* def_type = ts_node_type(def_node);
        if (strcmp(def_type, "function_definition") == 0) {
            PyAstNode* func = build_py_function_def(tp, def_node);
            if (func) {
                ((PyFunctionDefNode*)func)->decorators = decorators;
            }
            return func;
        } else if (strcmp(def_type, "class_definition") == 0) {
            PyAstNode* cls = build_py_class_def(tp, def_node);
            if (cls) {
                ((PyClassDefNode*)cls)->decorators = decorators;
            }
            return cls;
        }
    }

    return NULL;
}

// ---- Main dispatch functions ----

PyAstNode* build_py_expression(PyTranspiler* tp, TSNode expr_node) {
    if (ts_node_is_null(expr_node)) return NULL;

    const char* node_type = ts_node_type(expr_node);

    // identifiers
    if (strcmp(node_type, "identifier") == 0) {
        return build_py_identifier(tp, expr_node);
    }

    // literals
    if (strcmp(node_type, "integer") == 0) {
        return build_py_integer(tp, expr_node);
    }
    if (strcmp(node_type, "float") == 0) {
        return build_py_float(tp, expr_node);
    }
    if (strcmp(node_type, "string") == 0) {
        return build_py_string(tp, expr_node);
    }
    if (strcmp(node_type, "concatenated_string") == 0) {
        // concatenated strings: "a" "b" → "ab"
        // build first string part for now
        uint32_t child_count = ts_node_named_child_count(expr_node);
        if (child_count > 0) {
            return build_py_string(tp, ts_node_named_child(expr_node, 0));
        }
        return build_py_none(tp, expr_node);
    }
    if (strcmp(node_type, "true") == 0) {
        return build_py_boolean(tp, expr_node, true);
    }
    if (strcmp(node_type, "false") == 0) {
        return build_py_boolean(tp, expr_node, false);
    }
    if (strcmp(node_type, "none") == 0) {
        return build_py_none(tp, expr_node);
    }
    if (strcmp(node_type, "ellipsis") == 0) {
        return build_py_none(tp, expr_node); // ... maps to None for now
    }

    // operators
    if (strcmp(node_type, "binary_operator") == 0) {
        return build_py_binary_op(tp, expr_node);
    }
    if (strcmp(node_type, "unary_operator") == 0) {
        return build_py_unary_op(tp, expr_node);
    }
    if (strcmp(node_type, "boolean_operator") == 0) {
        return build_py_boolean_op(tp, expr_node);
    }
    if (strcmp(node_type, "not_operator") == 0) {
        return build_py_not_op(tp, expr_node);
    }
    if (strcmp(node_type, "comparison_operator") == 0) {
        return build_py_comparison(tp, expr_node);
    }
    if (strcmp(node_type, "conditional_expression") == 0) {
        return build_py_conditional_expr(tp, expr_node);
    }

    // calls and access
    if (strcmp(node_type, "call") == 0) {
        return build_py_call(tp, expr_node);
    }
    if (strcmp(node_type, "attribute") == 0) {
        return build_py_attribute(tp, expr_node);
    }
    if (strcmp(node_type, "subscript") == 0) {
        return build_py_subscript(tp, expr_node);
    }

    // collections
    if (strcmp(node_type, "list") == 0) {
        return build_py_sequence(tp, expr_node, PY_AST_NODE_LIST);
    }
    if (strcmp(node_type, "tuple") == 0) {
        return build_py_sequence(tp, expr_node, PY_AST_NODE_TUPLE);
    }
    if (strcmp(node_type, "set") == 0) {
        return build_py_sequence(tp, expr_node, PY_AST_NODE_SET);
    }
    if (strcmp(node_type, "dictionary") == 0) {
        return build_py_dict(tp, expr_node);
    }

    // comprehensions
    if (strcmp(node_type, "list_comprehension") == 0) {
        return build_py_list_comprehension(tp, expr_node, PY_AST_NODE_LIST_COMPREHENSION);
    }
    if (strcmp(node_type, "dictionary_comprehension") == 0) {
        return build_py_list_comprehension(tp, expr_node, PY_AST_NODE_DICT_COMPREHENSION);
    }
    if (strcmp(node_type, "set_comprehension") == 0) {
        return build_py_list_comprehension(tp, expr_node, PY_AST_NODE_SET_COMPREHENSION);
    }
    if (strcmp(node_type, "generator_expression") == 0) {
        return build_py_list_comprehension(tp, expr_node, PY_AST_NODE_GENERATOR_EXPRESSION);
    }

    // lambda
    if (strcmp(node_type, "lambda") == 0) {
        return build_py_lambda(tp, expr_node);
    }

    // parenthesized expression — unwrap
    if (strcmp(node_type, "parenthesized_expression") == 0) {
        if (ts_node_named_child_count(expr_node) > 0) {
            return build_py_expression(tp, ts_node_named_child(expr_node, 0));
        }
        return NULL;
    }

    // starred expression (*args)
    if (strcmp(node_type, "list_splat") == 0) {
        PyStarredNode* starred = (PyStarredNode*)alloc_py_ast_node(tp, PY_AST_NODE_STARRED, expr_node, sizeof(PyStarredNode));
        if (ts_node_named_child_count(expr_node) > 0) {
            starred->value = build_py_expression(tp, ts_node_named_child(expr_node, 0));
        }
        starred->base.type = &TYPE_ANY;
        return (PyAstNode*)starred;
    }

    // tuple expression (top-level a, b, c)
    if (strcmp(node_type, "expression_list") == 0 || strcmp(node_type, "pattern_list") == 0) {
        return build_py_sequence(tp, expr_node, PY_AST_NODE_TUPLE);
    }

    // named expression (walrus operator :=)
    if (strcmp(node_type, "named_expression") == 0) {
        // treat as assignment expression
        PyAssignmentNode* assign = (PyAssignmentNode*)alloc_py_ast_node(tp, PY_AST_NODE_ASSIGNMENT, expr_node, sizeof(PyAssignmentNode));
        TSNode name = ts_node_child_by_field_name(expr_node, "name", 4);
        TSNode value = ts_node_child_by_field_name(expr_node, "value", 5);
        assign->targets = build_py_expression(tp, name);
        assign->value = build_py_expression(tp, value);
        assign->base.type = &TYPE_ANY;
        return (PyAstNode*)assign;
    }

    // await expression
    if (strcmp(node_type, "await") == 0) {
        if (ts_node_named_child_count(expr_node) > 0) {
            return build_py_expression(tp, ts_node_named_child(expr_node, 0));
        }
        return NULL;
    }

    // yield expression
    if (strcmp(node_type, "yield") == 0) {
        // build the yielded value
        if (ts_node_named_child_count(expr_node) > 0) {
            return build_py_expression(tp, ts_node_named_child(expr_node, 0));
        }
        return build_py_none(tp, expr_node);
    }

    // type node (in annotations) — just return the inner expression
    if (strcmp(node_type, "type") == 0) {
        if (ts_node_named_child_count(expr_node) > 0) {
            return build_py_expression(tp, ts_node_named_child(expr_node, 0));
        }
        return NULL;
    }

    // slice (if encountered as expression outside subscript)
    if (strcmp(node_type, "slice") == 0) {
        return build_py_slice(tp, expr_node);
    }

    // keyword argument (in call context)
    if (strcmp(node_type, "keyword_argument") == 0) {
        PyKeywordArgNode* kw = (PyKeywordArgNode*)alloc_py_ast_node(tp, PY_AST_NODE_KEYWORD_ARGUMENT, expr_node, sizeof(PyKeywordArgNode));
        TSNode name = ts_node_child_by_field_name(expr_node, "name", 4);
        TSNode value = ts_node_child_by_field_name(expr_node, "value", 5);
        if (!ts_node_is_null(name)) {
            StrView name_src = py_node_source(tp, name);
            kw->key = name_pool_create_len(tp->name_pool, name_src.str, name_src.length);
        }
        kw->value = build_py_expression(tp, value);
        kw->base.type = &TYPE_ANY;
        return (PyAstNode*)kw;
    }

    // pair (dict literal key:value)
    if (strcmp(node_type, "pair") == 0) {
        PyPairNode* pair = (PyPairNode*)alloc_py_ast_node(tp, PY_AST_NODE_PAIR, expr_node, sizeof(PyPairNode));
        TSNode key = ts_node_child_by_field_name(expr_node, "key", 3);
        TSNode value = ts_node_child_by_field_name(expr_node, "value", 5);
        pair->key = build_py_expression(tp, key);
        pair->value = build_py_expression(tp, value);
        pair->base.type = &TYPE_ANY;
        return (PyAstNode*)pair;
    }

    log_debug("py: unhandled expression type: %s", node_type);
    return NULL;
}

// ============================================================================
// Phase B: match/case pattern matching builders
// ============================================================================

// Allocate a PyPatternNode with the given kind
static PyPatternNode* alloc_pattern_node(PyTranspiler* tp, PyPatternKind kind, TSNode node) {
    PyPatternNode* p = (PyPatternNode*)alloc_py_ast_node(tp, PY_AST_NODE_PATTERN, node, sizeof(PyPatternNode));
    p->kind = kind;
    p->rest_pos = -1;  // -1 = no star in sequence
    return p;
}

// Forward declaration
static PyAstNode* build_py_pattern(PyTranspiler* tp, TSNode node);

// Return the concrete type inside a case_pattern wrapper.
// If node is already a concrete type, returns node unchanged.
static TSNode py_case_pattern_unwrap(TSNode node) {
    if (!ts_node_is_null(node) && strcmp(ts_node_type(node), "case_pattern") == 0) {
        if (ts_node_named_child_count(node) > 0)
            return ts_node_named_child(node, 0);
    }
    return node;
}

// Check if a case_pattern node contains a keyword_pattern (for class pattern keyword args)
static bool py_case_pattern_is_keyword(TSNode case_pat_node) {
    if (ts_node_named_child_count(case_pat_node) == 0) return false;
    TSNode inner = ts_node_named_child(case_pat_node, 0);
    return strcmp(ts_node_type(inner), "keyword_pattern") == 0;
}

// Build a literal PyAstNode (expression) from a pattern literal node
static PyAstNode* build_literal_from_node(PyTranspiler* tp, TSNode node) {
    const char* nt = ts_node_type(node);
    if (strcmp(nt, "integer") == 0)     return build_py_expression(tp, node);
    if (strcmp(nt, "float") == 0)       return build_py_expression(tp, node);
    if (strcmp(nt, "string") == 0)      return build_py_expression(tp, node);
    if (strcmp(nt, "concatenated_string") == 0) return build_py_expression(tp, node);
    if (strcmp(nt, "true") == 0)        return build_py_expression(tp, node);
    if (strcmp(nt, "false") == 0)       return build_py_expression(tp, node);
    if (strcmp(nt, "none") == 0)        return build_py_expression(tp, node);
    return build_py_expression(tp, node);
}

// Recursively build a pattern node from any pattern-related TSNode
static PyAstNode* build_py_pattern(PyTranspiler* tp, TSNode node) {
    if (ts_node_is_null(node)) return NULL;
    const char* nt = ts_node_type(node);

    // unwrap case_pattern outer wrapper
    if (strcmp(nt, "case_pattern") == 0) {
        uint32_t nc = ts_node_named_child_count(node);
        if (nc == 0) {
            // Either wildcard '_' or empty case (treat as wildcard)
            return (PyAstNode*)alloc_pattern_node(tp, PY_PAT_WILDCARD, node);
        }
        // Check for anonymous '-' sign before named child (negative number literal)
        bool has_neg = false;
        uint32_t all = ts_node_child_count(node);
        for (uint32_t i = 0; i < all; i++) {
            TSNode c = ts_node_child(node, i);
            if (!ts_node_is_named(c) && strcmp(ts_node_type(c), "-") == 0) {
                has_neg = true;
                break;
            }
        }
        PyAstNode* pat = build_py_pattern(tp, ts_node_named_child(node, 0));
        if (pat && has_neg && ((PyPatternNode*)pat)->kind == PY_PAT_LITERAL) {
            ((PyPatternNode*)pat)->literal_neg = true;
        }
        return pat;
    }

    // as_pattern: <inner_pattern> 'as' <identifier>
    if (strcmp(nt, "as_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_AS, node);
        uint32_t nc = ts_node_named_child_count(node);
        if (nc >= 1) {
            p->literal = build_py_pattern(tp, ts_node_named_child(node, 0));
        }
        if (nc >= 2) {
            TSNode alias_node = ts_node_named_child(node, nc - 1);
            StrView sv = py_node_source(tp, alias_node);
            char* tmp = (char*)malloc(sv.length + 1);
            if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
        }
        return (PyAstNode*)p;
    }

    // keyword_pattern: <identifier> '=' <simple_pattern> (inside class_pattern)
    // Represented as PAT_CAPTURE with name=attr, literal=sub-pattern
    if (strcmp(nt, "keyword_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_CAPTURE, node);
        uint32_t nc = ts_node_named_child_count(node);
        if (nc >= 1) {
            TSNode attr = ts_node_named_child(node, 0);
            StrView sv = py_node_source(tp, attr);
            char* tmp = (char*)malloc(sv.length + 1);
            if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
        }
        if (nc >= 2) {
            p->literal = build_py_pattern(tp, ts_node_named_child(node, 1));
        }
        return (PyAstNode*)p;
    }

    // union_pattern: p1 | p2 | ... (flat linked list stored in elements)
    if (strcmp(nt, "union_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_OR, node);
        uint32_t nc = ts_node_named_child_count(node);
        PyAstNode* prev = NULL;
        for (uint32_t i = 0; i < nc; i++) {
            PyAstNode* alt = build_py_pattern(tp, ts_node_named_child(node, i));
            if (!alt) continue;
            if (!p->elements) p->elements = alt;
            else prev->next = alt;
            prev = alt;
        }
        return (PyAstNode*)p;
    }

    // list_pattern or tuple_pattern: [p1, p2, *rest]
    if (strcmp(nt, "list_pattern") == 0 || strcmp(nt, "tuple_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_SEQUENCE, node);
        p->rest_pos = -1;
        int elem_idx = 0;
        uint32_t nc = ts_node_named_child_count(node);
        PyAstNode* prev = NULL;
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i);
            PyAstNode* sub = build_py_pattern(tp, child);
            if (!sub) continue;
            if (((PyPatternNode*)sub)->kind == PY_PAT_STAR) {
                p->rest_name = ((PyPatternNode*)sub)->name; // may be NULL for *_
                p->rest_pos = elem_idx; // mark where star is
                continue; // star is a marker, not added to elements
            }
            if (!p->elements) p->elements = sub;
            else prev->next = sub;
            prev = sub;
            elem_idx++;
        }
        return (PyAstNode*)p;
    }

    // splat_pattern: *name or **name or *_
    if (strcmp(nt, "splat_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_STAR, node);
        uint32_t nc = ts_node_named_child_count(node);
        if (nc >= 1) {
            TSNode child = ts_node_named_child(node, 0);
            // identifier node (could be "_" text)
            StrView sv = py_node_source(tp, child);
            if (!(sv.length == 1 && sv.str[0] == '_')) {
                char* tmp = (char*)malloc(sv.length + 1);
                if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                    p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
            }
        } else {
            // anonymous '_' child (not a named identifier)
            p->name = NULL;
        }
        return (PyAstNode*)p;
    }

    // dict_pattern: {key: val, **rest}
    // _key_value_pattern is transparent: its 'key' and 'value' fields are elevated to dict_pattern.
    // named children alternate: key0, val0, key1, val1, ..., (optional splat_pattern)
    if (strcmp(nt, "dict_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_MAPPING, node);
        uint32_t nc = ts_node_named_child_count(node);
        PyAstNode* prev_kv = NULL;
        uint32_t ki = 0; // index among non-splat children
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "splat_pattern") == 0) {
                // **rest — extract rest name
                uint32_t sn = ts_node_named_child_count(child);
                if (sn >= 1) {
                    TSNode id = ts_node_named_child(child, 0);
                    StrView sv = py_node_source(tp, id);
                    if (!(sv.length == 1 && sv.str[0] == '_')) {
                        char* tmp = (char*)malloc(sv.length + 1);
                        if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                            p->rest_name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
                    }
                }
                continue;
            }
            if (ki % 2 == 0) {
                // even index = key pattern; build KV node at next iteration
                // stash as a temporary; we'll create the pair on the value iteration
                // store key in a temporary PyPatternKVNode allocated from pool
                PyPatternKVNode* kv = (PyPatternKVNode*)alloc_py_ast_node(tp, PY_AST_NODE_PATTERN, child, sizeof(PyPatternKVNode));
                kv->kind = PY_PAT_LITERAL;
                kv->key_pat = build_py_pattern(tp, child);
                // link into kv_pairs now; val_pat will be set next iteration
                if (!p->kv_pairs) p->kv_pairs = (PyAstNode*)kv;
                else prev_kv->next = (PyAstNode*)kv;
                prev_kv = (PyAstNode*)kv;
            } else {
                // odd index = value pattern
                if (prev_kv) {
                    ((PyPatternKVNode*)prev_kv)->val_pat = build_py_pattern(tp, child);
                }
            }
            ki++;
        }
        return (PyAstNode*)p;
    }

    // class_pattern: ClassName(positional_pats..., attr=pat...)
    if (strcmp(nt, "class_pattern") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_CLASS, node);
        uint32_t nc = ts_node_named_child_count(node);
        if (nc >= 1) {
            TSNode cls_name_node = ts_node_named_child(node, 0);
            StrView sv = py_node_source(tp, cls_name_node);
            char* tmp = (char*)malloc(sv.length + 1);
            if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
        }
        PyAstNode* prev_pos = NULL;
        PyAstNode* prev_kw = NULL;
        for (uint32_t i = 1; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i); // each is a case_pattern
            if (py_case_pattern_is_keyword(child)) {
                // keyword pattern: attr=sub_pattern
                TSNode kw_inner = ts_node_named_child(child, 0); // keyword_pattern node
                PyAstNode* kw_pat = build_py_pattern(tp, kw_inner);
                if (!kw_pat) continue;
                // keyword_pattern built as PAT_CAPTURE: name=attr, literal=sub
                if (!p->kv_pairs) p->kv_pairs = kw_pat;
                else prev_kw->next = kw_pat;
                prev_kw = kw_pat;
            } else {
                PyAstNode* sub = build_py_pattern(tp, child);
                if (!sub) continue;
                if (!p->elements) p->elements = sub;
                else prev_pos->next = sub;
                prev_pos = sub;
            }
        }
        return (PyAstNode*)p;
    }

    // literal patterns
    if (strcmp(nt, "integer") == 0 || strcmp(nt, "float") == 0 ||
        strcmp(nt, "string") == 0 || strcmp(nt, "concatenated_string") == 0 ||
        strcmp(nt, "true") == 0 || strcmp(nt, "false") == 0 || strcmp(nt, "none") == 0) {
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_LITERAL, node);
        p->literal = build_literal_from_node(tp, node);
        return (PyAstNode*)p;
    }

    // negative number at top level (e.g., inside union_pattern that strips case_pattern wrapper)
    // seq(optional('-'), integer/float) appears as children of the parent, handled by check above.
    // But if this node IS an integer/float directly, already handled above.

    // dotted_name: single identifier → CAPTURE or WILDCARD; multi-part → VALUE
    if (strcmp(nt, "dotted_name") == 0) {
        uint32_t nc = ts_node_named_child_count(node);
        if (nc == 1) {
            TSNode id_node = ts_node_named_child(node, 0);
            StrView sv = py_node_source(tp, id_node);
            if (sv.length == 1 && sv.str[0] == '_') {
                return (PyAstNode*)alloc_pattern_node(tp, PY_PAT_WILDCARD, node);
            }
            PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_CAPTURE, node);
            char* tmp = (char*)malloc(sv.length + 1);
            if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
            return (PyAstNode*)p;
        } else {
            // dotted name like Status.OK → VALUE pattern
            PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_VALUE, node);
            StrView sv = py_node_source(tp, node); // full dotted text "Status.OK"
            char* tmp = (char*)malloc(sv.length + 1);
            if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
                p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
            return (PyAstNode*)p;
        }
    }

    // plain identifier (shouldn't normally appear at pattern level, but guard against it)
    if (strcmp(nt, "identifier") == 0) {
        StrView sv = py_node_source(tp, node);
        if (sv.length == 1 && sv.str[0] == '_') {
            return (PyAstNode*)alloc_pattern_node(tp, PY_PAT_WILDCARD, node);
        }
        PyPatternNode* p = alloc_pattern_node(tp, PY_PAT_CAPTURE, node);
        char* tmp = (char*)malloc(sv.length + 1);
        if (tmp) { memcpy(tmp, sv.str, sv.length); tmp[sv.length] = '\0';
            p->name = name_pool_create_len(tp->name_pool, tmp, sv.length); free(tmp); }
        return (PyAstNode*)p;
    }

    log_debug("py: unhandled pattern node type: %s", nt);
    return NULL;
}

// Build a case_clause node from tree-sitter node
static PyAstNode* build_py_case_clause(PyTranspiler* tp, TSNode clause_node) {
    PyCaseNode* c = (PyCaseNode*)alloc_py_ast_node(tp, PY_AST_NODE_CASE, clause_node, sizeof(PyCaseNode));

    // Collect case_pattern children (typically one, sometimes comma-separated tuple)
    uint32_t nc = ts_node_named_child_count(clause_node);
    PyAstNode* first_pattern = NULL;
    PyAstNode* prev_pattern = NULL;

    for (uint32_t i = 0; i < nc; i++) {
        TSNode child = ts_node_named_child(clause_node, i);
        const char* ct = ts_node_type(child);
        if (strcmp(ct, "case_pattern") == 0) {
            PyAstNode* pat = build_py_pattern(tp, child);
            if (!pat) continue;
            if (!first_pattern) first_pattern = pat;
            else prev_pattern->next = pat;
            prev_pattern = pat;
        }
    }

    // If multiple patterns (comma-separated e.g. case a, b:), wrap in SEQUENCE
    if (first_pattern && first_pattern->next) {
        PyPatternNode* tuple_pat = alloc_pattern_node(tp, PY_PAT_SEQUENCE, clause_node);
        tuple_pat->elements = first_pattern;
        c->pattern = (PyAstNode*)tuple_pat;
    } else {
        c->pattern = first_pattern;
    }

    // guard: optional if_clause
    TSNode guard_node = ts_node_child_by_field_name(clause_node, "guard", 5);
    if (!ts_node_is_null(guard_node)) {
        // if_clause: seq('if', expression) — first named child is the condition
        uint32_t gn = ts_node_named_child_count(guard_node);
        if (gn >= 1) {
            c->guard = build_py_expression(tp, ts_node_named_child(guard_node, 0));
        }
    }

    // consequence: the body block
    TSNode body_node = ts_node_child_by_field_name(clause_node, "consequence", 11);
    if (!ts_node_is_null(body_node)) {
        c->body = build_py_block(tp, body_node);
    }

    c->base.type = &TYPE_ANY;
    return (PyAstNode*)c;
}

// Build a match statement node
static PyAstNode* build_py_match_statement(PyTranspiler* tp, TSNode match_node) {
    PyMatchNode* m = (PyMatchNode*)alloc_py_ast_node(tp, PY_AST_NODE_MATCH, match_node, sizeof(PyMatchNode));

    // subject: commaSep1(expression) — may have multiple subject fields
    // Use the first subject field (tuple subject is rare)
    TSNode subject_node = ts_node_child_by_field_name(match_node, "subject", 7);
    if (!ts_node_is_null(subject_node)) {
        m->subject = build_py_expression(tp, subject_node);
    }

    // body: block containing case_clause children
    TSNode body_node = ts_node_child_by_field_name(match_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        uint32_t nc = ts_node_named_child_count(body_node);
        PyAstNode* prev = NULL;
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(body_node, i);
            if (strcmp(ts_node_type(child), "case_clause") == 0) {
                PyAstNode* clause = build_py_case_clause(tp, child);
                if (!clause) continue;
                if (!m->cases) m->cases = clause;
                else prev->next = clause;
                prev = clause;
            }
        }
    }

    m->base.type = &TYPE_ANY;
    return (PyAstNode*)m;
}

// ============================================================================

PyAstNode* build_py_statement(PyTranspiler* tp, TSNode stmt_node) {
    if (ts_node_is_null(stmt_node)) return NULL;

    const char* node_type = ts_node_type(stmt_node);

    // skip comments
    if (strcmp(node_type, "comment") == 0) return NULL;

    // direct assignment / augmented assignment (may appear outside expression_statement)
    if (strcmp(node_type, "assignment") == 0) {
        return build_py_assignment(tp, stmt_node);
    }
    if (strcmp(node_type, "augmented_assignment") == 0) {
        return build_py_augmented_assignment(tp, stmt_node);
    }

    // expression statement
    if (strcmp(node_type, "expression_statement") == 0) {
        uint32_t child_count = ts_node_named_child_count(stmt_node);
        if (child_count == 0) return NULL;

        TSNode child = ts_node_named_child(stmt_node, 0);
        const char* child_type = ts_node_type(child);

        // assignment and augmented assignment are children of expression_statement
        if (strcmp(child_type, "assignment") == 0) {
            return build_py_assignment(tp, child);
        }
        if (strcmp(child_type, "augmented_assignment") == 0) {
            return build_py_augmented_assignment(tp, child);
        }

        // regular expression statement
        PyExpressionStatementNode* expr_stmt = (PyExpressionStatementNode*)alloc_py_ast_node(
            tp, PY_AST_NODE_EXPRESSION_STATEMENT, stmt_node, sizeof(PyExpressionStatementNode));
        expr_stmt->expression = build_py_expression(tp, child);
        expr_stmt->base.type = &TYPE_ANY;
        return (PyAstNode*)expr_stmt;
    }

    // compound statements
    if (strcmp(node_type, "if_statement") == 0) {
        return build_py_if_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "while_statement") == 0) {
        return build_py_while_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "for_statement") == 0) {
        return build_py_for_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "function_definition") == 0) {
        return build_py_function_def(tp, stmt_node);
    }
    if (strcmp(node_type, "class_definition") == 0) {
        return build_py_class_def(tp, stmt_node);
    }
    if (strcmp(node_type, "try_statement") == 0) {
        return build_py_try_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "with_statement") == 0) {
        return build_py_with_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "match_statement") == 0) {
        return build_py_match_statement(tp, stmt_node);
    }
    if (strcmp(node_type, "decorated_definition") == 0) {
        return build_py_decorated_definition(tp, stmt_node);
    }

    // simple statements
    if (strcmp(node_type, "return_statement") == 0) {
        return build_py_return(tp, stmt_node);
    }
    if (strcmp(node_type, "pass_statement") == 0) {
        return alloc_py_ast_node(tp, PY_AST_NODE_PASS, stmt_node, sizeof(PyAstNode));
    }
    if (strcmp(node_type, "break_statement") == 0) {
        return alloc_py_ast_node(tp, PY_AST_NODE_BREAK, stmt_node, sizeof(PyAstNode));
    }
    if (strcmp(node_type, "continue_statement") == 0) {
        return alloc_py_ast_node(tp, PY_AST_NODE_CONTINUE, stmt_node, sizeof(PyAstNode));
    }
    if (strcmp(node_type, "raise_statement") == 0) {
        return build_py_raise(tp, stmt_node);
    }
    if (strcmp(node_type, "assert_statement") == 0) {
        return build_py_assert(tp, stmt_node);
    }
    if (strcmp(node_type, "global_statement") == 0) {
        return build_py_global_nonlocal(tp, stmt_node, PY_AST_NODE_GLOBAL);
    }
    if (strcmp(node_type, "nonlocal_statement") == 0) {
        return build_py_global_nonlocal(tp, stmt_node, PY_AST_NODE_NONLOCAL);
    }
    if (strcmp(node_type, "delete_statement") == 0) {
        return build_py_del(tp, stmt_node);
    }
    if (strcmp(node_type, "import_statement") == 0) {
        return build_py_import(tp, stmt_node);
    }
    if (strcmp(node_type, "import_from_statement") == 0) {
        return build_py_import_from(tp, stmt_node);
    }

    // block (standalone)
    if (strcmp(node_type, "block") == 0) {
        return build_py_block(tp, stmt_node);
    }

    // fallback: try as expression
    PyAstNode* expr = build_py_expression(tp, stmt_node);
    if (expr) {
        PyExpressionStatementNode* expr_stmt = (PyExpressionStatementNode*)alloc_py_ast_node(
            tp, PY_AST_NODE_EXPRESSION_STATEMENT, stmt_node, sizeof(PyExpressionStatementNode));
        expr_stmt->expression = expr;
        expr_stmt->base.type = &TYPE_ANY;
        return (PyAstNode*)expr_stmt;
    }

    log_debug("py: unhandled statement type: %s", node_type);
    return NULL;
}

// Build module (top-level program)
PyAstNode* build_py_module(PyTranspiler* tp, TSNode module_node) {
    PyModuleNode* module = (PyModuleNode*)alloc_py_ast_node(tp, PY_AST_NODE_MODULE, module_node, sizeof(PyModuleNode));

    uint32_t child_count = ts_node_named_child_count(module_node);
    PyAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(module_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "comment") == 0) continue;

        PyAstNode* stmt = build_py_statement(tp, child);
        if (stmt) {
            if (!prev) {
                module->body = stmt;
            } else {
                prev->next = stmt;
            }
            prev = stmt;
        }
    }

    return (PyAstNode*)module;
}

// Main entry point: build AST from Tree-sitter root
PyAstNode* build_py_ast(PyTranspiler* tp, TSNode root) {
    const char* root_type = ts_node_type(root);

    if (strcmp(root_type, "module") != 0) {
        log_error("py: expected 'module' root node, got '%s'", root_type);
        return NULL;
    }

    return build_py_module(tp, root);
}
