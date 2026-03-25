/**
 * Bash AST Builder
 *
 * Converts a Tree-sitter CST (Concrete Syntax Tree) from tree-sitter-bash
 * into a typed Bash AST for transpilation to MIR.
 *
 * NOTE: tree-sitter-bash must be integrated into the build system before
 * this file can be compiled. See doc/dev/Bash_Runtime.md Phase 1.
 */
#include "bash_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <tree_sitter/tree-sitter-bash.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

// utility: extract source text from a Tree-sitter node
#define bash_node_source(tp, node) \
    {.str = (tp)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node)}

// forward declarations
static BashAstNode* build_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_command(BashTranspiler* tp, TSNode node);
static BashAstNode* build_if_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_for_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_while_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_case_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_function_def(BashTranspiler* tp, TSNode node);
static BashAstNode* build_pipeline(BashTranspiler* tp, TSNode node);
static BashAstNode* build_list(BashTranspiler* tp, TSNode node);
static BashAstNode* build_subshell(BashTranspiler* tp, TSNode node);
static BashAstNode* build_compound_statement(BashTranspiler* tp, TSNode node);
static BashAstNode* build_word(BashTranspiler* tp, TSNode node);
static BashAstNode* build_string_node(BashTranspiler* tp, TSNode node);
static BashAstNode* build_raw_string(BashTranspiler* tp, TSNode node);
static BashAstNode* build_expansion(BashTranspiler* tp, TSNode node);
static BashAstNode* build_command_substitution(BashTranspiler* tp, TSNode node);
static BashAstNode* build_arith_expression(BashTranspiler* tp, TSNode node);
static BashAstNode* build_test_command(BashTranspiler* tp, TSNode node);
static BashAstNode* build_assignment(BashTranspiler* tp, TSNode node);
static BashAstNode* build_declaration(BashTranspiler* tp, TSNode node);
static BashAstNode* build_redirected(BashTranspiler* tp, TSNode node);
static BashAstNode* build_heredoc(BashTranspiler* tp, TSNode node);
static BashAstNode* build_concatenation(BashTranspiler* tp, TSNode node);
static BashAstNode* build_array(BashTranspiler* tp, TSNode node);
static BashAstNode* build_variable_ref(BashTranspiler* tp, TSNode node);
static BashAstNode* build_special_variable(BashTranspiler* tp, TSNode node);

// ============================================================================
// Allocation
// ============================================================================

BashAstNode* alloc_bash_ast_node(BashTranspiler* tp, BashAstNodeType node_type, TSNode node, size_t size) {
    BashAstNode* ast_node = (BashAstNode*)pool_alloc(tp->ast_pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;
    ast_node->node = node;
    return ast_node;
}

// create a String* from a Tree-sitter node's source text
static String* node_text(BashTranspiler* tp, TSNode node) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;
    const char* src = tp->source + start;
    return name_pool_create_len(tp->name_pool, src, len);
}

// ============================================================================
// Operator conversion
// ============================================================================

BashOperator bash_operator_from_string(const char* op_str, size_t len) {
    if (len == 1) {
        switch (op_str[0]) {
        case '+': return BASH_OP_ADD;
        case '-': return BASH_OP_SUB;
        case '*': return BASH_OP_MUL;
        case '/': return BASH_OP_DIV;
        case '%': return BASH_OP_MOD;
        case '<': return BASH_OP_LT;
        case '>': return BASH_OP_GT;
        case '&': return BASH_OP_BIT_AND;
        case '|': return BASH_OP_BIT_OR;
        case '^': return BASH_OP_BIT_XOR;
        case '~': return BASH_OP_BIT_NOT;
        case '!': return BASH_OP_LOGICAL_NOT;
        case '=': return BASH_OP_ASSIGN;
        }
    } else if (len == 2) {
        if (memcmp(op_str, "**", 2) == 0) return BASH_OP_POW;
        if (memcmp(op_str, "==", 2) == 0) return BASH_OP_EQ;
        if (memcmp(op_str, "!=", 2) == 0) return BASH_OP_NE;
        if (memcmp(op_str, "<=", 2) == 0) return BASH_OP_LE;
        if (memcmp(op_str, ">=", 2) == 0) return BASH_OP_GE;
        if (memcmp(op_str, "<<", 2) == 0) return BASH_OP_LSHIFT;
        if (memcmp(op_str, ">>", 2) == 0) return BASH_OP_RSHIFT;
        if (memcmp(op_str, "&&", 2) == 0) return BASH_OP_LOGICAL_AND;
        if (memcmp(op_str, "||", 2) == 0) return BASH_OP_LOGICAL_OR;
        if (memcmp(op_str, "++", 2) == 0) return BASH_OP_INC;
        if (memcmp(op_str, "--", 2) == 0) return BASH_OP_DEC;
        if (memcmp(op_str, "+=", 2) == 0) return BASH_OP_ADD_ASSIGN;
        if (memcmp(op_str, "-=", 2) == 0) return BASH_OP_SUB_ASSIGN;
        if (memcmp(op_str, "*=", 2) == 0) return BASH_OP_MUL_ASSIGN;
        if (memcmp(op_str, "/=", 2) == 0) return BASH_OP_DIV_ASSIGN;
        if (memcmp(op_str, "%=", 2) == 0) return BASH_OP_MOD_ASSIGN;
    }
    log_error("bash: unknown operator: %.*s", (int)len, op_str);
    return BASH_OP_ADD;
}

BashTestOp bash_test_op_from_string(const char* op_str, size_t len) {
    if (len == 3) {
        if (memcmp(op_str, "-eq", 3) == 0) return BASH_TEST_EQ;
        if (memcmp(op_str, "-ne", 3) == 0) return BASH_TEST_NE;
        if (memcmp(op_str, "-gt", 3) == 0) return BASH_TEST_GT;
        if (memcmp(op_str, "-ge", 3) == 0) return BASH_TEST_GE;
        if (memcmp(op_str, "-lt", 3) == 0) return BASH_TEST_LT;
        if (memcmp(op_str, "-le", 3) == 0) return BASH_TEST_LE;
    }
    if (len == 2) {
        if (memcmp(op_str, "-z", 2) == 0) return BASH_TEST_Z;
        if (memcmp(op_str, "-n", 2) == 0) return BASH_TEST_N;
        if (memcmp(op_str, "-f", 2) == 0) return BASH_TEST_F;
        if (memcmp(op_str, "-d", 2) == 0) return BASH_TEST_D;
        if (memcmp(op_str, "-e", 2) == 0) return BASH_TEST_E;
        if (memcmp(op_str, "-r", 2) == 0) return BASH_TEST_R;
        if (memcmp(op_str, "-w", 2) == 0) return BASH_TEST_W;
        if (memcmp(op_str, "-x", 2) == 0) return BASH_TEST_X;
        if (memcmp(op_str, "-s", 2) == 0) return BASH_TEST_S;
        if (memcmp(op_str, "-L", 2) == 0) return BASH_TEST_L;
        if (memcmp(op_str, "==", 2) == 0) return BASH_TEST_STR_EQ;
        if (memcmp(op_str, "!=", 2) == 0) return BASH_TEST_STR_NE;
        if (memcmp(op_str, "=~", 2) == 0) return BASH_TEST_STR_MATCH;
        if (memcmp(op_str, "&&", 2) == 0) return BASH_TEST_AND;
        if (memcmp(op_str, "||", 2) == 0) return BASH_TEST_OR;
    }
    if (len == 1) {
        if (op_str[0] == '=') return BASH_TEST_STR_EQ;
        if (op_str[0] == '<') return BASH_TEST_STR_LT;
        if (op_str[0] == '>') return BASH_TEST_STR_GT;
        if (op_str[0] == '!') return BASH_TEST_NOT;
    }
    log_error("bash: unknown test operator: %.*s", (int)len, op_str);
    return BASH_TEST_EQ;
}

// ============================================================================
// Statement builders
// ============================================================================

static BashAstNode* build_statement(BashTranspiler* tp, TSNode node) {
    const char* type = ts_node_type(node);

    if (strcmp(type, "command") == 0) return build_command(tp, node);
    if (strcmp(type, "if_statement") == 0) return build_if_statement(tp, node);
    if (strcmp(type, "for_statement") == 0) return build_for_statement(tp, node);
    if (strcmp(type, "c_style_for_statement") == 0) return build_for_statement(tp, node);
    if (strcmp(type, "while_statement") == 0) return build_while_statement(tp, node);
    if (strcmp(type, "case_statement") == 0) return build_case_statement(tp, node);
    if (strcmp(type, "function_definition") == 0) return build_function_def(tp, node);
    if (strcmp(type, "pipeline") == 0) return build_pipeline(tp, node);
    if (strcmp(type, "list") == 0) return build_list(tp, node);
    if (strcmp(type, "subshell") == 0) return build_subshell(tp, node);
    if (strcmp(type, "compound_statement") == 0) return build_compound_statement(tp, node);
    if (strcmp(type, "variable_assignment") == 0) return build_assignment(tp, node);
    if (strcmp(type, "declaration_command") == 0) return build_declaration(tp, node);
    if (strcmp(type, "redirected_statement") == 0) return build_redirected(tp, node);
    if (strcmp(type, "test_command") == 0) return build_test_command(tp, node);
    if (strcmp(type, "negated_command") == 0) {
        // ! command
        uint32_t child_count = ts_node_named_child_count(node);
        if (child_count > 0) {
            TSNode child = ts_node_named_child(node, 0);
            BashAstNode* inner = build_statement(tp, child);
            // wrap in a pipeline with negated flag
            BashPipelineNode* pipeline = (BashPipelineNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_PIPELINE, node, sizeof(BashPipelineNode));
            pipeline->commands = inner;
            pipeline->command_count = 1;
            pipeline->negated = true;
            return (BashAstNode*)pipeline;
        }
        return NULL;
    }

    // for any unrecognized node type, log and skip
    if (!ts_node_is_named(node)) return NULL;

    log_debug("bash: unhandled statement type: %s", type);
    return NULL;
}

// ============================================================================
// Command builder
// ============================================================================

static BashAstNode* build_command(BashTranspiler* tp, TSNode node) {
    // check if this is a break/continue/return/exit command
    uint32_t nc = ts_node_named_child_count(node);
    if (nc >= 1) {
        TSNode first = ts_node_named_child(node, 0);
        const char* first_type = ts_node_type(first);
        if (strcmp(first_type, "command_name") == 0 || strcmp(first_type, "word") == 0) {
            StrView name_src = bash_node_source(tp, first);
            if (name_src.length == 5 && strncmp(name_src.str, "break", 5) == 0) {
                BashControlNode* ctrl = (BashControlNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_BREAK, node, sizeof(BashControlNode));
                return (BashAstNode*)ctrl;
            }
            if (name_src.length == 8 && strncmp(name_src.str, "continue", 8) == 0) {
                BashControlNode* ctrl = (BashControlNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_CONTINUE, node, sizeof(BashControlNode));
                return (BashAstNode*)ctrl;
            }
            if (name_src.length == 6 && strncmp(name_src.str, "return", 6) == 0) {
                BashControlNode* ctrl = (BashControlNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_RETURN, node, sizeof(BashControlNode));
                if (nc >= 2) {
                    TSNode val = ts_node_named_child(node, 1);
                    ctrl->value = build_word(tp, val);
                }
                return (BashAstNode*)ctrl;
            }
            if (name_src.length == 4 && strncmp(name_src.str, "exit", 4) == 0) {
                BashControlNode* ctrl = (BashControlNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_EXIT, node, sizeof(BashControlNode));
                if (nc >= 2) {
                    TSNode val = ts_node_named_child(node, 1);
                    ctrl->value = build_word(tp, val);
                }
                return (BashAstNode*)ctrl;
            }
        }
    }

    BashCommandNode* cmd = (BashCommandNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_COMMAND, node, sizeof(BashCommandNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* args_tail = NULL;
    cmd->arg_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        BashAstNode* child_ast = NULL;

        if (strcmp(child_type, "word") == 0 ||
            strcmp(child_type, "number") == 0) {
            child_ast = build_word(tp, child);
        } else if (strcmp(child_type, "string") == 0) {
            child_ast = build_string_node(tp, child);
        } else if (strcmp(child_type, "raw_string") == 0) {
            child_ast = build_raw_string(tp, child);
        } else if (strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0) {
            child_ast = build_expansion(tp, child);
        } else if (strcmp(child_type, "command_substitution") == 0) {
            child_ast = build_command_substitution(tp, child);
        } else if (strcmp(child_type, "concatenation") == 0) {
            child_ast = build_concatenation(tp, child);
        } else if (strcmp(child_type, "arithmetic_expansion") == 0) {
            child_ast = build_arith_expression(tp, child);
        } else if (strcmp(child_type, "variable_assignment") == 0) {
            // prefix assignment: VAR=value cmd
            BashAstNode* assign = build_assignment(tp, child);
            if (assign) {
                assign->next = cmd->assignments;
                cmd->assignments = assign;
            }
            continue;
        } else {
            child_ast = build_word(tp, child);
        }

        if (!child_ast) continue;

        if (!cmd->name) {
            cmd->name = child_ast;
        } else {
            if (!cmd->args) {
                cmd->args = child_ast;
            } else {
                args_tail->next = child_ast;
            }
            args_tail = child_ast;
            cmd->arg_count++;
        }
    }

    return (BashAstNode*)cmd;
}

// ============================================================================
// Word / string / expression builders
// ============================================================================

static BashAstNode* build_word(BashTranspiler* tp, TSNode node) {
    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
    word->text = node_text(tp, node);
    return (BashAstNode*)word;
}

// dispatch builder based on tree-sitter node type (for contexts like test operands)
static BashAstNode* build_expr_node(BashTranspiler* tp, TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "simple_expansion") == 0 || strcmp(type, "expansion") == 0) {
        return build_expansion(tp, node);
    } else if (strcmp(type, "string") == 0) {
        return build_string_node(tp, node);
    } else if (strcmp(type, "raw_string") == 0) {
        return build_raw_string(tp, node);
    } else if (strcmp(type, "command_substitution") == 0) {
        return build_command_substitution(tp, node);
    } else if (strcmp(type, "concatenation") == 0) {
        return build_concatenation(tp, node);
    }
    return build_word(tp, node);
}

static BashAstNode* build_string_node(BashTranspiler* tp, TSNode node) {
    // double-quoted string — may contain expansions
    // tree-sitter-bash can include leading whitespace in expansion node ranges,
    // so we use byte-position tracking to reliably capture all literal text.
    BashStringNode* str = (BashStringNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_STRING, node, sizeof(BashStringNode));

    uint32_t str_start = ts_node_start_byte(node) + 1; // skip opening "
    uint32_t str_end = ts_node_end_byte(node) - 1;       // skip closing "
    BashAstNode* parts_tail = NULL;

    auto append_part = [&](BashAstNode* part) {
        if (!part) return;
        if (!str->parts) {
            str->parts = part;
        } else {
            parts_tail->next = part;
        }
        parts_tail = part;
    };

    uint32_t named_count = ts_node_named_child_count(node);
    if (named_count == 0) {
        if (str_end > str_start) {
            BashWordNode* literal = (BashWordNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
            literal->text = name_pool_create_len(tp->name_pool, tp->source + str_start, str_end - str_start);
            str->parts = (BashAstNode*)literal;
        }
        return (BashAstNode*)str;
    }

    uint32_t pos = str_start;
    for (uint32_t i = 0; i < named_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "simple_expansion") == 0 ||
            strcmp(child_type, "expansion") == 0) {
            // find the actual $ position (tree-sitter may include leading whitespace)
            uint32_t dollar_pos = ts_node_start_byte(child);
            uint32_t child_end_byte = ts_node_end_byte(child);
            while (dollar_pos < child_end_byte && tp->source[dollar_pos] != '$') {
                dollar_pos++;
            }
            if (dollar_pos > pos) {
                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, dollar_pos - pos);
                append_part((BashAstNode*)gap);
            }
            append_part(build_expansion(tp, child));
            pos = child_end_byte;
        } else if (strcmp(child_type, "command_substitution") == 0) {
            uint32_t cs_start = ts_node_start_byte(child);
            if (cs_start > pos) {
                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, cs_start - pos);
                append_part((BashAstNode*)gap);
            }
            append_part(build_command_substitution(tp, child));
            pos = ts_node_end_byte(child);
        } else if (strcmp(child_type, "string_content") == 0) {
            uint32_t sc_start = ts_node_start_byte(child);
            uint32_t real_start = (sc_start < pos) ? pos : sc_start;
            uint32_t sc_end = ts_node_end_byte(child);
            if (sc_end > real_start) {
                BashWordNode* literal = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                literal->text = name_pool_create_len(tp->name_pool, tp->source + real_start, sc_end - real_start);
                append_part((BashAstNode*)literal);
            }
            pos = sc_end;
        } else {
            uint32_t c_start = ts_node_start_byte(child);
            uint32_t c_end = ts_node_end_byte(child);
            if (c_end > c_start) {
                BashWordNode* literal = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                literal->text = name_pool_create_len(tp->name_pool, tp->source + c_start, c_end - c_start);
                append_part((BashAstNode*)literal);
            }
            if (c_end > pos) pos = c_end;
        }
    }

    // trailing literal text after the last expansion
    if (pos < str_end) {
        BashWordNode* tail = (BashWordNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
        tail->text = name_pool_create_len(tp->name_pool, tp->source + pos, str_end - pos);
        append_part((BashAstNode*)tail);
    }

    return (BashAstNode*)str;
}

static BashAstNode* build_raw_string(BashTranspiler* tp, TSNode node) {
    // single-quoted string — no expansion
    BashRawStringNode* raw = (BashRawStringNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_RAW_STRING, node, sizeof(BashRawStringNode));

    // strip surrounding quotes
    uint32_t start = ts_node_start_byte(node) + 1;
    uint32_t end = ts_node_end_byte(node) - 1;
    if (end > start) {
        raw->text = name_pool_create_len(tp->name_pool, tp->source + start, end - start);
    } else {
        raw->text = name_pool_create_len(tp->name_pool, "", 0);
    }
    return (BashAstNode*)raw;
}

static BashAstNode* build_concatenation(BashTranspiler* tp, TSNode node) {
    BashConcatNode* concat = (BashConcatNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_CONCATENATION, node, sizeof(BashConcatNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* parts_tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        BashAstNode* part = NULL;

        if (strcmp(child_type, "word") == 0) {
            part = build_word(tp, child);
        } else if (strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0) {
            part = build_expansion(tp, child);
        } else if (strcmp(child_type, "string") == 0) {
            part = build_string_node(tp, child);
        } else if (strcmp(child_type, "raw_string") == 0) {
            part = build_raw_string(tp, child);
        } else if (strcmp(child_type, "command_substitution") == 0) {
            part = build_command_substitution(tp, child);
        } else {
            part = build_word(tp, child);
        }

        if (!part) continue;
        if (!concat->parts) {
            concat->parts = part;
        } else {
            parts_tail->next = part;
        }
        parts_tail = part;
    }

    return (BashAstNode*)concat;
}

// ============================================================================
// Variable references and expansions
// ============================================================================

static BashAstNode* build_variable_ref(BashTranspiler* tp, TSNode node) {
    BashVarRefNode* ref = (BashVarRefNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_VARIABLE_REF, node, sizeof(BashVarRefNode));

    // try to get variable name from named child (more reliable than raw source)
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "variable_name") == 0) {
            ref->name = node_text(tp, child);
            return (BashAstNode*)ref;
        }
    }

    // fallback: extract variable name from source (skip $ and {} wrappers)
    StrView source = bash_node_source(tp, node);
    const char* name_start = source.str;
    size_t name_len = source.length;
    if (name_len > 0 && name_start[0] == '$') {
        name_start++;
        name_len--;
    }
    if (name_len > 0 && name_start[0] == '{') {
        name_start++;
        name_len--;
        if (name_len > 0 && name_start[name_len - 1] == '}') {
            name_len--;
        }
    }
    ref->name = name_pool_create_len(tp->name_pool, name_start, name_len);
    return (BashAstNode*)ref;
}

static BashAstNode* build_special_variable(BashTranspiler* tp, TSNode node) {
    BashSpecialVarNode* special = (BashSpecialVarNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_SPECIAL_VARIABLE, node, sizeof(BashSpecialVarNode));

    StrView source = bash_node_source(tp, node);
    // skip $ prefix
    const char* name = source.str + 1;
    size_t len = source.length - 1;

    if (len == 1) {
        switch (name[0]) {
        case '?': special->special_id = BASH_SPECIAL_QUESTION; break;
        case '#': special->special_id = BASH_SPECIAL_HASH; break;
        case '@': special->special_id = BASH_SPECIAL_AT; break;
        case '*': special->special_id = BASH_SPECIAL_STAR; break;
        case '$': special->special_id = BASH_SPECIAL_DOLLAR; break;
        case '!': special->special_id = BASH_SPECIAL_BANG; break;
        case '-': special->special_id = BASH_SPECIAL_DASH; break;
        case '0': special->special_id = BASH_SPECIAL_ZERO; break;
        case '1': special->special_id = BASH_SPECIAL_POS_1; break;
        case '2': special->special_id = BASH_SPECIAL_POS_2; break;
        case '3': special->special_id = BASH_SPECIAL_POS_3; break;
        case '4': special->special_id = BASH_SPECIAL_POS_4; break;
        case '5': special->special_id = BASH_SPECIAL_POS_5; break;
        case '6': special->special_id = BASH_SPECIAL_POS_6; break;
        case '7': special->special_id = BASH_SPECIAL_POS_7; break;
        case '8': special->special_id = BASH_SPECIAL_POS_8; break;
        case '9': special->special_id = BASH_SPECIAL_POS_9; break;
        default: special->special_id = BASH_SPECIAL_QUESTION; break;
        }
    }

    return (BashAstNode*)special;
}

static BashAstNode* build_expansion(BashTranspiler* tp, TSNode node) {
    const char* node_type = ts_node_type(node);

    if (strcmp(node_type, "simple_expansion") == 0) {
        // $var or $N or $? etc.
        uint32_t child_count = ts_node_named_child_count(node);
        if (child_count > 0) {
            TSNode child = ts_node_named_child(node, 0);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "special_variable_name") == 0) {
                return build_special_variable(tp, node);
            }
        }
        return build_variable_ref(tp, node);
    }

    // complex expansion: ${var:-default}, ${var##pat}, etc.
    BashExpansionNode* expansion = (BashExpansionNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_EXPANSION, node, sizeof(BashExpansionNode));

    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        TSNode var_node = ts_node_named_child(node, 0);
        expansion->variable = node_text(tp, var_node);
    }

    // determine expansion type from the operator
    // tree-sitter-bash will annotate the operator in child nodes
    // for now, detect from source text
    StrView source = bash_node_source(tp, node);
    const char* src = source.str;
    size_t len = source.length;

    // detect expansion type by scanning for operator
    if (len >= 4 && src[0] == '$' && src[1] == '{') {
        if (src[2] == '#') {
            // ${#var} — string length
            expansion->expand_type = BASH_EXPAND_LENGTH;
            expansion->variable = name_pool_create_len(tp->name_pool, src + 3, len - 4);
        } else {
            // scan for the operator after the variable name
            expansion->expand_type = BASH_EXPAND_DEFAULT;  // fallback
        }
    }

    return (BashAstNode*)expansion;
}

static BashAstNode* build_command_substitution(BashTranspiler* tp, TSNode node) {
    BashCommandSubNode* cmd_sub = (BashCommandSubNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_COMMAND_SUB, node, sizeof(BashCommandSubNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* body_tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        BashAstNode* stmt = build_statement(tp, child);
        if (!stmt) continue;

        if (!cmd_sub->body) {
            cmd_sub->body = stmt;
        } else {
            body_tail->next = stmt;
        }
        body_tail = stmt;
    }

    return (BashAstNode*)cmd_sub;
}

// ============================================================================
// Control flow builders
// ============================================================================

static BashAstNode* build_block(BashTranspiler* tp, TSNode node) {
    BashBlockNode* block = (BashBlockNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_BLOCK, node, sizeof(BashBlockNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        BashAstNode* stmt = build_statement(tp, child);
        if (!stmt) continue;

        if (!block->statements) {
            block->statements = stmt;
        } else {
            tail->next = stmt;
        }
        tail = stmt;
    }

    return (BashAstNode*)block;
}

static BashAstNode* build_if_statement(BashTranspiler* tp, TSNode node) {
    BashIfNode* if_node = (BashIfNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_IF, node, sizeof(BashIfNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* elif_tail = NULL;
    bool found_condition = false;
    BashBlockNode* then_block = NULL;
    BashAstNode* then_tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "elif_clause") == 0) {
            BashElifNode* elif = (BashElifNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ELIF, child, sizeof(BashElifNode));

            // elif has condition + body children
            uint32_t elif_children = ts_node_named_child_count(child);
            bool elif_found_cond = false;
            BashBlockNode* elif_block = NULL;
            BashAstNode* elif_body_tail = NULL;
            for (uint32_t j = 0; j < elif_children; j++) {
                TSNode elif_child = ts_node_named_child(child, j);
                if (!elif_found_cond) {
                    elif->condition = build_statement(tp, elif_child);
                    elif_found_cond = true;
                } else {
                    BashAstNode* stmt = build_statement(tp, elif_child);
                    if (stmt) {
                        if (!elif_block) {
                            elif_block = (BashBlockNode*)alloc_bash_ast_node(
                                tp, BASH_AST_NODE_BLOCK, child, sizeof(BashBlockNode));
                        }
                        if (!elif_block->statements) {
                            elif_block->statements = stmt;
                        } else {
                            elif_body_tail->next = stmt;
                        }
                        elif_body_tail = stmt;
                    }
                }
            }
            elif->body = (BashAstNode*)elif_block;

            if (!if_node->elif_clauses) {
                if_node->elif_clauses = (BashAstNode*)elif;
            } else {
                elif_tail->next = (BashAstNode*)elif;
            }
            elif_tail = (BashAstNode*)elif;
        } else if (strcmp(child_type, "else_clause") == 0) {
            uint32_t else_children = ts_node_named_child_count(child);
            BashBlockNode* else_block = (BashBlockNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_BLOCK, child, sizeof(BashBlockNode));
            BashAstNode* else_tail = NULL;
            for (uint32_t j = 0; j < else_children; j++) {
                TSNode else_child = ts_node_named_child(child, j);
                BashAstNode* stmt = build_statement(tp, else_child);
                if (stmt) {
                    if (!else_block->statements) {
                        else_block->statements = stmt;
                    } else {
                        else_tail->next = stmt;
                    }
                    else_tail = stmt;
                }
            }
            if_node->else_body = (BashAstNode*)else_block;
        } else {
            // condition or then-body statement
            if (!found_condition) {
                if_node->condition = build_statement(tp, child);
                found_condition = true;
            } else {
                // accumulate then-body statements
                BashAstNode* stmt = build_statement(tp, child);
                if (stmt) {
                    if (!then_block) {
                        then_block = (BashBlockNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_BLOCK, child, sizeof(BashBlockNode));
                    }
                    if (!then_block->statements) {
                        then_block->statements = stmt;
                    } else {
                        then_tail->next = stmt;
                    }
                    then_tail = stmt;
                }
            }
        }
    }

    if_node->then_body = (BashAstNode*)then_block;
    return (BashAstNode*)if_node;
}

static BashAstNode* build_for_statement(BashTranspiler* tp, TSNode node) {
    const char* type = ts_node_type(node);

    if (strcmp(type, "c_style_for_statement") == 0) {
        // for (( init; cond; step ))
        BashForArithNode* for_arith = (BashForArithNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_FOR_ARITHMETIC, node, sizeof(BashForArithNode));

        // tree-sitter-bash: c_style_for_statement children include
        // the arithmetic expressions and the body
        uint32_t child_count = ts_node_named_child_count(node);
        int arith_idx = 0;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            const char* child_type = ts_node_type(child);

            if (strcmp(child_type, "do_group") == 0 ||
                strcmp(child_type, "compound_statement") == 0) {
                for_arith->body = build_block(tp, child);
            } else {
                BashAstNode* arith = build_arith_expression(tp, child);
                if (arith_idx == 0) for_arith->init = arith;
                else if (arith_idx == 1) for_arith->condition = arith;
                else if (arith_idx == 2) for_arith->step = arith;
                arith_idx++;
            }
        }

        return (BashAstNode*)for_arith;
    }

    // for var in list
    BashForNode* for_node = (BashForNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_FOR, node, sizeof(BashForNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* words_tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        log_debug("bash-ast: for_statement child[%d] type='%s'", i, child_type);

        if (strcmp(child_type, "variable_name") == 0) {
            for_node->variable = node_text(tp, child);
        } else if (strcmp(child_type, "do_group") == 0 ||
                   strcmp(child_type, "compound_statement") == 0) {
            for_node->body = build_block(tp, child);
        } else if (strcmp(child_type, "word") == 0 ||
                   strcmp(child_type, "string") == 0 ||
                   strcmp(child_type, "raw_string") == 0 ||
                   strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0 ||
                   strcmp(child_type, "number") == 0) {
            BashAstNode* word;
            if (strcmp(child_type, "string") == 0) word = build_string_node(tp, child);
            else if (strcmp(child_type, "raw_string") == 0) word = build_raw_string(tp, child);
            else if (strcmp(child_type, "simple_expansion") == 0 ||
                     strcmp(child_type, "expansion") == 0) word = build_expansion(tp, child);
            else word = build_word(tp, child);

            if (word) {
                if (!for_node->words) {
                    for_node->words = word;
                } else {
                    words_tail->next = word;
                }
                words_tail = word;
            }
        }
    }

    return (BashAstNode*)for_node;
}

static BashAstNode* build_while_statement(BashTranspiler* tp, TSNode node) {
    // tree-sitter-bash uses while_statement for both while and until
    // distinguish by checking the first token (anonymous child "while" or "until")
    BashAstNodeType ast_type = BASH_AST_NODE_WHILE;
    TSNode first_child = ts_node_child(node, 0);
    if (!ts_node_is_null(first_child)) {
        StrView kw = bash_node_source(tp, first_child);
        if (kw.length == 5 && strncmp(kw.str, "until", 5) == 0) {
            ast_type = BASH_AST_NODE_UNTIL;
        }
    }

    // tree-sitter-bash uses different node type for until
    // handle it the same way but note the difference
    BashWhileNode* while_node = (BashWhileNode*)alloc_bash_ast_node(
        tp, ast_type, node, sizeof(BashWhileNode));

    uint32_t child_count = ts_node_named_child_count(node);
    bool found_condition = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "do_group") == 0 ||
            strcmp(child_type, "compound_statement") == 0) {
            while_node->body = build_block(tp, child);
        } else if (!found_condition) {
            while_node->condition = build_statement(tp, child);
            found_condition = true;
        }
    }

    return (BashAstNode*)while_node;
}

static BashAstNode* build_case_statement(BashTranspiler* tp, TSNode node) {
    BashCaseNode* case_node = (BashCaseNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_CASE, node, sizeof(BashCaseNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* items_tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "word") == 0 ||
            strcmp(child_type, "simple_expansion") == 0 ||
            strcmp(child_type, "string") == 0) {
            if (!case_node->word) {
                if (strcmp(child_type, "simple_expansion") == 0)
                    case_node->word = build_expansion(tp, child);
                else
                    case_node->word = build_word(tp, child);
            }
        } else if (strcmp(child_type, "case_item") == 0) {
            BashCaseItemNode* item = (BashCaseItemNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_CASE_ITEM, child, sizeof(BashCaseItemNode));

            uint32_t item_children = ts_node_named_child_count(child);
            BashAstNode* patterns_tail = NULL;
            bool found_patterns = false;

            for (uint32_t j = 0; j < item_children; j++) {
                TSNode item_child = ts_node_named_child(child, j);
                const char* item_child_type = ts_node_type(item_child);

                if (strcmp(item_child_type, "word") == 0 ||
                    strcmp(item_child_type, "extglob_pattern") == 0) {
                    BashAstNode* pattern = build_word(tp, item_child);
                    if (pattern) {
                        if (!item->patterns) {
                            item->patterns = pattern;
                        } else {
                            patterns_tail->next = pattern;
                        }
                        patterns_tail = pattern;
                        found_patterns = true;
                    }
                } else if (found_patterns) {
                    // the rest is the body
                    BashAstNode* stmt = build_statement(tp, item_child);
                    if (stmt) {
                        if (!item->body) {
                            item->body = stmt;
                        } else {
                            // append to body chain
                            BashAstNode* tail = item->body;
                            while (tail->next) tail = tail->next;
                            tail->next = stmt;
                        }
                    }
                }
            }

            if (!case_node->items) {
                case_node->items = (BashAstNode*)item;
            } else {
                items_tail->next = (BashAstNode*)item;
            }
            items_tail = (BashAstNode*)item;
        }
    }

    return (BashAstNode*)case_node;
}

// ============================================================================
// Function definition
// ============================================================================

static BashAstNode* build_function_def(BashTranspiler* tp, TSNode node) {
    BashFunctionDefNode* func = (BashFunctionDefNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_FUNCTION_DEF, node, sizeof(BashFunctionDefNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "word") == 0) {
            func->name = node_text(tp, child);
        } else if (strcmp(child_type, "compound_statement") == 0 ||
                   strcmp(child_type, "subshell") == 0) {
            func->body = build_block(tp, child);
        }
    }

    // register function in scope
    if (func->name) {
        bash_scope_define(tp, func->name, (BashAstNode*)func, BASH_VAR_GLOBAL);
    }

    return (BashAstNode*)func;
}

// ============================================================================
// Pipeline, list, subshell, compound
// ============================================================================

static BashAstNode* build_pipeline(BashTranspiler* tp, TSNode node) {
    BashPipelineNode* pipeline = (BashPipelineNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_PIPELINE, node, sizeof(BashPipelineNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* tail = NULL;
    pipeline->command_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        BashAstNode* cmd = build_statement(tp, child);
        if (!cmd) continue;

        if (!pipeline->commands) {
            pipeline->commands = cmd;
        } else {
            tail->next = cmd;
        }
        tail = cmd;
        pipeline->command_count++;
    }

    return (BashAstNode*)pipeline;
}

static BashAstNode* build_list(BashTranspiler* tp, TSNode node) {
    // list: cmd1 && cmd2 || cmd3
    // tree-sitter-bash uses binary tree for list nodes
    BashListNode* list = (BashListNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_LIST, node, sizeof(BashListNode));

    uint32_t child_count = ts_node_child_count(node);
    // find the operator between commands
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_named(child)) {
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "&&") == 0) list->op = BASH_LIST_AND;
            else if (strcmp(child_type, "||") == 0) list->op = BASH_LIST_OR;
            else if (strcmp(child_type, ";") == 0) list->op = BASH_LIST_SEMI;
            else if (strcmp(child_type, "&") == 0) list->op = BASH_LIST_BG;
        }
    }

    uint32_t named_count = ts_node_named_child_count(node);
    if (named_count >= 2) {
        list->left = build_statement(tp, ts_node_named_child(node, 0));
        list->right = build_statement(tp, ts_node_named_child(node, 1));
    } else if (named_count == 1) {
        list->left = build_statement(tp, ts_node_named_child(node, 0));
    }

    return (BashAstNode*)list;
}

static BashAstNode* build_subshell(BashTranspiler* tp, TSNode node) {
    BashSubshellNode* subshell = (BashSubshellNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_SUBSHELL, node, sizeof(BashSubshellNode));
    subshell->body = build_block(tp, node);
    return (BashAstNode*)subshell;
}

static BashAstNode* build_compound_statement(BashTranspiler* tp, TSNode node) {
    BashCompoundNode* compound = (BashCompoundNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_COMPOUND_STATEMENT, node, sizeof(BashCompoundNode));
    compound->body = build_block(tp, node);
    return (BashAstNode*)compound;
}

// ============================================================================
// Assignments
// ============================================================================

static BashAstNode* build_assignment(BashTranspiler* tp, TSNode node) {
    BashAssignmentNode* assign = (BashAssignmentNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_ASSIGNMENT, node, sizeof(BashAssignmentNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "variable_name") == 0) {
            assign->name = node_text(tp, child);
        } else if (strcmp(child_type, "word") == 0 ||
                   strcmp(child_type, "number") == 0) {
            assign->value = build_word(tp, child);
        } else if (strcmp(child_type, "string") == 0) {
            assign->value = build_string_node(tp, child);
        } else if (strcmp(child_type, "raw_string") == 0) {
            assign->value = build_raw_string(tp, child);
        } else if (strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0) {
            assign->value = build_expansion(tp, child);
        } else if (strcmp(child_type, "command_substitution") == 0) {
            assign->value = build_command_substitution(tp, child);
        } else if (strcmp(child_type, "concatenation") == 0) {
            assign->value = build_concatenation(tp, child);
        } else if (strcmp(child_type, "arithmetic_expansion") == 0) {
            assign->value = build_arith_expression(tp, child);
        } else if (strcmp(child_type, "array") == 0) {
            assign->value = build_array(tp, child);
        }
    }

    // detect += (compound assignment)
    StrView source = bash_node_source(tp, node);
    for (size_t i = 0; i < source.length - 1; i++) {
        if (source.str[i] == '+' && source.str[i + 1] == '=') {
            assign->is_append = true;
            break;
        }
    }

    // register in scope
    if (assign->name) {
        bash_scope_define(tp, assign->name, (BashAstNode*)assign, BASH_VAR_GLOBAL);
    }

    return (BashAstNode*)assign;
}

static BashAstNode* build_declaration(BashTranspiler* tp, TSNode node) {
    // local var=value, export var=value
    // first child is the keyword (local, export, declare, typeset)
    StrView source = bash_node_source(tp, node);
    bool is_local = (source.length >= 5 && strncmp(source.str, "local", 5) == 0);
    bool is_export = (source.length >= 6 && strncmp(source.str, "export", 6) == 0);

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* result = NULL;
    BashAstNode* tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "variable_assignment") == 0) {
            BashAssignmentNode* assign = (BashAssignmentNode*)build_assignment(tp, child);
            if (assign) {
                assign->is_local = is_local;
                assign->is_export = is_export;
                if (assign->name) {
                    BashVarKind kind = is_local ? BASH_VAR_LOCAL :
                                      is_export ? BASH_VAR_EXPORT : BASH_VAR_GLOBAL;
                    bash_scope_define(tp, assign->name, (BashAstNode*)assign, kind);
                }
                if (!result) {
                    result = (BashAstNode*)assign;
                } else {
                    tail->next = (BashAstNode*)assign;
                }
                tail = (BashAstNode*)assign;
            }
        } else if (strcmp(child_type, "word") == 0 ||
                   strcmp(child_type, "variable_name") == 0) {
            // declaration without assignment: local var, export var
            BashAssignmentNode* assign = (BashAssignmentNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ASSIGNMENT, child, sizeof(BashAssignmentNode));
            assign->name = node_text(tp, child);
            assign->is_local = is_local;
            assign->is_export = is_export;
            if (assign->name) {
                BashVarKind kind = is_local ? BASH_VAR_LOCAL :
                                  is_export ? BASH_VAR_EXPORT : BASH_VAR_GLOBAL;
                bash_scope_define(tp, assign->name, (BashAstNode*)assign, kind);
            }
            if (!result) {
                result = (BashAstNode*)assign;
            } else {
                tail->next = (BashAstNode*)assign;
            }
            tail = (BashAstNode*)assign;
        }
    }

    return result;
}

// ============================================================================
// Arithmetic expression
// ============================================================================

static BashAstNode* build_arith_expression(BashTranspiler* tp, TSNode node) {
    const char* node_type = ts_node_type(node);

    // handle leaf types directly (when called recursively on binary_expression operands)
    if (strcmp(node_type, "number") == 0) {
        BashArithNumberNode* num = (BashArithNumberNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_NUMBER, node, sizeof(BashArithNumberNode));
        StrView src = bash_node_source(tp, node);
        num->value = strtoll(src.str, NULL, 10);
        return (BashAstNode*)num;
    }
    if (strcmp(node_type, "variable_name") == 0 || strcmp(node_type, "word") == 0) {
        BashArithVariableNode* var = (BashArithVariableNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_VARIABLE, node, sizeof(BashArithVariableNode));
        var->name = node_text(tp, node);
        return (BashAstNode*)var;
    }
    if (strcmp(node_type, "simple_expansion") == 0) {
        return build_expansion(tp, node);
    }
    if (strcmp(node_type, "unary_expression") == 0) {
        BashArithUnaryNode* unary = (BashArithUnaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_UNARY, node, sizeof(BashArithUnaryNode));
        uint32_t ch_count = ts_node_child_count(node);
        for (uint32_t j = 0; j < ch_count; j++) {
            TSNode uc = ts_node_child(node, j);
            if (!ts_node_is_named(uc)) {
                StrView op_src = bash_node_source(tp, uc);
                unary->op = bash_operator_from_string(op_src.str, op_src.length);
            } else {
                unary->operand = build_arith_expression(tp, uc);
            }
        }
        return (BashAstNode*)unary;
    }
    if (strcmp(node_type, "binary_expression") == 0) {
        BashArithBinaryNode* bin = (BashArithBinaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_BINARY, node, sizeof(BashArithBinaryNode));
        uint32_t bin_children = ts_node_child_count(node);
        int operand_idx = 0;
        for (uint32_t j = 0; j < bin_children; j++) {
            TSNode bc = ts_node_child(node, j);
            if (!ts_node_is_named(bc)) {
                StrView op_src = bash_node_source(tp, bc);
                bin->op = bash_operator_from_string(op_src.str, op_src.length);
            } else {
                if (operand_idx == 0) {
                    bin->left = build_arith_expression(tp, bc);
                    operand_idx++;
                } else {
                    bin->right = build_arith_expression(tp, bc);
                }
            }
        }
        return (BashAstNode*)bin;
    }
    if (strcmp(node_type, "parenthesized_expression") == 0) {
        // ( expr ) — unwrap and recurse into the inner expression
        uint32_t ch_count = ts_node_named_child_count(node);
        if (ch_count > 0) {
            return build_arith_expression(tp, ts_node_named_child(node, 0));
        }
        return NULL;
    }

    // for arithmetic_expansion or other wrapper types, recurse into children
    BashArithExprNode* arith = (BashArithExprNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_ARITHMETIC_EXPR, node, sizeof(BashArithExprNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        arith->expression = build_arith_expression(tp, child);
    }

    return (BashAstNode*)arith;
}

// ============================================================================
// Test command
// ============================================================================

static BashAstNode* build_test_command(BashTranspiler* tp, TSNode node) {
    BashTestCommandNode* test = (BashTestCommandNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_TEST_COMMAND, node, sizeof(BashTestCommandNode));

    // detect [[ ]] vs [ ]
    StrView source = bash_node_source(tp, node);
    bool is_extended = (source.length >= 4 &&
                        source.str[0] == '[' && source.str[1] == '[');
    if (is_extended) {
        test->base.node_type = BASH_AST_NODE_EXTENDED_TEST;
    }

    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count == 1) {
        // single expression
        TSNode child = ts_node_named_child(node, 0);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "binary_expression") == 0) {
            BashTestBinaryNode* bin = (BashTestBinaryNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_TEST_BINARY, child, sizeof(BashTestBinaryNode));

            // use field API — grammar defines field('left'), field('operator'), field('right')
            TSNode op_node = ts_node_child_by_field_name(child, "operator", 8);
            if (!ts_node_is_null(op_node)) {
                StrView op_src = bash_node_source(tp, op_node);
                bin->op = bash_test_op_from_string(op_src.str, op_src.length);
            }
            TSNode left_node = ts_node_child_by_field_name(child, "left", 4);
            if (!ts_node_is_null(left_node)) {
                bin->left = build_expr_node(tp, left_node);
            }
            TSNode right_node = ts_node_child_by_field_name(child, "right", 5);
            if (!ts_node_is_null(right_node)) {
                bin->right = build_expr_node(tp, right_node);
            }
            test->expression = (BashAstNode*)bin;
        } else if (strcmp(child_type, "unary_expression") == 0) {
            BashTestUnaryNode* unary = (BashTestUnaryNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_TEST_UNARY, child, sizeof(BashTestUnaryNode));

            // use field API
            TSNode op_node = ts_node_child_by_field_name(child, "operator", 8);
            if (!ts_node_is_null(op_node)) {
                StrView op_src = bash_node_source(tp, op_node);
                unary->op = bash_test_op_from_string(op_src.str, op_src.length);
            }
            // operand: get all children except the operator (use first named non-operator child)
            uint32_t un_children = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < un_children; j++) {
                TSNode uc = ts_node_named_child(child, j);
                const char* uc_type = ts_node_type(uc);
                if (strcmp(uc_type, "test_operator") != 0) {
                    unary->operand = build_expr_node(tp, uc);
                    break;
                }
            }
            test->expression = (BashAstNode*)unary;
        } else {
            test->expression = build_expr_node(tp, child);
        }
    }

    return (BashAstNode*)test;
}

// ============================================================================
// Array literal
// ============================================================================

static BashAstNode* build_array(BashTranspiler* tp, TSNode node) {
    BashArrayLiteralNode* arr = (BashArrayLiteralNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_ARRAY_LITERAL, node, sizeof(BashArrayLiteralNode));

    uint32_t child_count = ts_node_named_child_count(node);
    BashAstNode* tail = NULL;
    arr->length = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        BashAstNode* elem;
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "string") == 0) elem = build_string_node(tp, child);
        else if (strcmp(child_type, "raw_string") == 0) elem = build_raw_string(tp, child);
        else elem = build_word(tp, child);

        if (elem) {
            if (!arr->elements) {
                arr->elements = elem;
            } else {
                tail->next = elem;
            }
            tail = elem;
            arr->length++;
        }
    }

    return (BashAstNode*)arr;
}

// ============================================================================
// Redirect, heredoc (stubs for now)
// ============================================================================

static BashAstNode* build_redirected(BashTranspiler* tp, TSNode node) {
    // redirected_statement wraps a command with redirections
    // for now, just build the inner command
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "file_redirect") == 0 ||
            strcmp(child_type, "heredoc_redirect") == 0 ||
            strcmp(child_type, "herestring_redirect") == 0) {
            // skip redirections for now
            continue;
        }
        return build_statement(tp, child);
    }
    return NULL;
}

static BashAstNode* build_heredoc(BashTranspiler* tp, TSNode node) {
    BashHeredocNode* heredoc = (BashHeredocNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_HEREDOC, node, sizeof(BashHeredocNode));
    // TODO: parse heredoc delimiter and body
    return (BashAstNode*)heredoc;
}

// ============================================================================
// Public API: build AST from program root
// ============================================================================

BashAstNode* build_bash_ast(BashTranspiler* tp, TSNode root) {
    return build_bash_program(tp, root);
}

BashAstNode* build_bash_program(BashTranspiler* tp, TSNode program_node) {
    BashProgramNode* program = (BashProgramNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_PROGRAM, program_node, sizeof(BashProgramNode));

    uint32_t child_count = ts_node_named_child_count(program_node);
    BashAstNode* tail = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(program_node, i);
        BashAstNode* stmt = build_statement(tp, child);
        if (!stmt) continue;

        if (!program->body) {
            program->body = stmt;
        } else {
            tail->next = stmt;
        }
        tail = stmt;
    }

    log_debug("bash: built AST program with %d top-level statements", child_count);
    return (BashAstNode*)program;
}

BashAstNode* build_bash_statement(BashTranspiler* tp, TSNode stmt_node) {
    return build_statement(tp, stmt_node);
}

BashAstNode* build_bash_command(BashTranspiler* tp, TSNode cmd_node) {
    return build_command(tp, cmd_node);
}

BashAstNode* build_bash_expression(BashTranspiler* tp, TSNode expr_node) {
    const char* type = ts_node_type(expr_node);
    if (strcmp(type, "simple_expansion") == 0 || strcmp(type, "expansion") == 0)
        return build_expansion(tp, expr_node);
    if (strcmp(type, "command_substitution") == 0)
        return build_command_substitution(tp, expr_node);
    if (strcmp(type, "string") == 0)
        return build_string_node(tp, expr_node);
    if (strcmp(type, "raw_string") == 0)
        return build_raw_string(tp, expr_node);
    return build_word(tp, expr_node);
}
