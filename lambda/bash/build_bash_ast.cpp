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
static BashAstNode* build_test_expression(BashTranspiler* tp, TSNode node);
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
    if (strcmp(type, "unset_command") == 0) {
        // unset var or unset 'arr[idx]'
        BashCommandNode* cmd = (BashCommandNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_COMMAND, node, sizeof(BashCommandNode));
        BashWordNode* name = (BashWordNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
        name->text = name_pool_create_len(tp->name_pool, "unset", 5);
        cmd->name = (BashAstNode*)name;
        cmd->arg_count = 0;
        BashAstNode* args_tail = NULL;
        uint32_t child_count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            const char* child_type = ts_node_type(child);
            BashAstNode* arg = NULL;
            if (strcmp(child_type, "word") == 0) arg = build_word(tp, child);
            else if (strcmp(child_type, "raw_string") == 0) arg = build_raw_string(tp, child);
            else if (strcmp(child_type, "string") == 0) arg = build_string_node(tp, child);
            else if (strcmp(child_type, "simple_expansion") == 0 ||
                     strcmp(child_type, "expansion") == 0) arg = build_expansion(tp, child);
            if (arg) {
                if (!cmd->args) cmd->args = arg; else args_tail->next = arg;
                args_tail = arg;
                cmd->arg_count++;
            }
        }
        return (BashAstNode*)cmd;
    }
    if (strcmp(type, "negated_command") == 0) {
        // ! command — negate the exit code
        uint32_t child_count = ts_node_named_child_count(node);
        if (child_count > 0) {
            TSNode child = ts_node_named_child(node, 0);
            BashAstNode* inner = build_statement(tp, child);
            if (inner && inner->node_type == BASH_AST_NODE_PIPELINE) {
                // inner is already a pipeline — just set its negated flag
                ((BashPipelineNode*)inner)->negated = true;
                return inner;
            }
            // wrap non-pipeline in a pipeline with negated flag
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

    // check for herestring_redirect child (cat <<< "str" parsed as command, not redirected_statement)
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "herestring_redirect") == 0) {
            // check if command name is "cat"
            if (i > 0) {
                TSNode first = ts_node_named_child(node, 0);
                StrView first_src = bash_node_source(tp, first);
                // command_name wraps the actual word
                if (strcmp(ts_node_type(first), "command_name") == 0 && ts_node_named_child_count(first) > 0) {
                    first_src = bash_node_source(tp, ts_node_named_child(first, 0));
                }
                if (first_src.length == 3 && memcmp(first_src.str, "cat", 3) == 0) {
                    BashHeredocNode* heredoc = (BashHeredocNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_HERESTRING, child, sizeof(BashHeredocNode));
                    uint32_t hsc = ts_node_named_child_count(child);
                    for (uint32_t j = 0; j < hsc; j++) {
                        TSNode hchild = ts_node_named_child(child, j);
                        const char* htype = ts_node_type(hchild);
                        if (strcmp(htype, "string") == 0) {
                            heredoc->body = build_string_node(tp, hchild);
                        } else if (strcmp(htype, "word") == 0) {
                            heredoc->body = build_word(tp, hchild);
                        } else if (strcmp(htype, "concatenation") == 0) {
                            heredoc->body = build_concatenation(tp, hchild);
                        }
                    }
                    heredoc->expand = true;
                    return (BashAstNode*)heredoc;
                }
            }
        }
    }

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
            uint32_t sc_end = ts_node_end_byte(child);
            // use pos (not sc_start) so gaps like newlines between string_content nodes are included
            if (sc_end > pos) {
                BashWordNode* literal = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                literal->text = name_pool_create_len(tp->name_pool, tp->source + pos, sc_end - pos);
                append_part((BashAstNode*)literal);
                pos = sc_end;
            }
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
    // check if the entire concatenation is a brace expansion pattern like {a,b,c} or {a..e}
    StrView src = bash_node_source(tp, node);
    if (src.length >= 3 && src.str[0] == '{' && src.str[src.length - 1] == '}') {
        bool is_brace = false;
        for (size_t i = 1; i < src.length - 1; i++) {
            if (src.str[i] == ',') { is_brace = true; break; }
            if (src.str[i] == '.' && i + 1 < src.length - 1 && src.str[i + 1] == '.') {
                is_brace = true; break;
            }
        }
        if (is_brace) {
            BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
            word->text = name_pool_create_len(tp->name_pool, src.str, (int)src.length);
            return (BashAstNode*)word;
        }
    }

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
    // find the $ prefix (tree-sitter may include leading whitespace)
    const char* dollar = source.str;
    size_t remaining = source.length;
    while (remaining > 0 && *dollar != '$') {
        dollar++;
        remaining--;
    }
    const char* name = dollar + 1;
    size_t len = remaining - 1;

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
            // check if variable_name is a positional param (single digit 0-9)
            if (strcmp(child_type, "variable_name") == 0) {
                StrView vn = bash_node_source(tp, child);
                if (vn.length == 1 && vn.str[0] >= '0' && vn.str[0] <= '9') {
                    return build_special_variable(tp, node);
                }
            }
        }
        return build_variable_ref(tp, node);
    }

    // complex expansion: ${var:-default}, ${var##pat}, etc.
    // first, check for array subscript: ${arr[idx]}, ${arr[@]}, ${#arr[@]}, ${arr[@]:off:len}
    {
        uint32_t total = ts_node_child_count(node);
        bool has_subscript = false;
        TSNode subscript_node = {0};
        bool has_hash_prefix = false;
        bool has_colon_op = false;
        BashAstNode* colon_arg1 = NULL;
        BashAstNode* colon_arg2 = NULL;

        // scan for subscript and prefix #
        for (uint32_t i = 0; i < total; i++) {
            TSNode ch = ts_node_child(node, i);
            const char* ch_type = ts_node_type(ch);
            bool is_named = ts_node_is_named(ch);
            if (is_named && strcmp(ch_type, "subscript") == 0) {
                has_subscript = true;
                subscript_node = ch;
            }
            if (!is_named) {
                StrView ch_src = bash_node_source(tp, ch);
                if (ch_src.length == 1 && ch_src.str[0] == '#' && !has_subscript) {
                    has_hash_prefix = true;
                }
                if (ch_src.length == 1 && ch_src.str[0] == ':' && has_subscript) {
                    has_colon_op = true;
                }
            }
            // collect arguments after colon
            if (has_colon_op && is_named && strcmp(ch_type, "subscript") != 0) {
                if (!colon_arg1) colon_arg1 = build_expr_node(tp, ch);
                else if (!colon_arg2) colon_arg2 = build_expr_node(tp, ch);
            }
        }

        if (has_subscript) {
            // extract name and index from subscript
            TSNode name_node = ts_node_child_by_field_name(subscript_node, "name", 4);
            TSNode index_node = ts_node_child_by_field_name(subscript_node, "index", 5);
            String* arr_name = node_text(tp, name_node);

            // check index for @ or *
            StrView idx_src = bash_node_source(tp, index_node);
            bool is_all = (idx_src.length == 1 && (idx_src.str[0] == '@' || idx_src.str[0] == '*'));

            if (has_hash_prefix && is_all) {
                // ${#arr[@]} — array length
                BashArrayLengthNode* len_node = (BashArrayLengthNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_ARRAY_LENGTH, node, sizeof(BashArrayLengthNode));
                len_node->name = arr_name;
                return (BashAstNode*)len_node;
            }

            if (has_colon_op && is_all) {
                // ${arr[@]:off:len} — array slice
                BashArraySliceNode* slice = (BashArraySliceNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_ARRAY_SLICE, node, sizeof(BashArraySliceNode));
                slice->name = arr_name;
                slice->offset = colon_arg1;
                slice->length = colon_arg2;
                return (BashAstNode*)slice;
            }

            if (is_all) {
                // ${arr[@]} or ${arr[*]} — all elements
                BashArrayAllNode* all_node = (BashArrayAllNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_ARRAY_ALL, node, sizeof(BashArrayAllNode));
                all_node->name = arr_name;
                return (BashAstNode*)all_node;
            }

            // ${arr[idx]} — indexed access
            BashArrayAccessNode* access = (BashArrayAccessNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ARRAY_ACCESS, node, sizeof(BashArrayAccessNode));
            access->name = arr_name;
            access->index = build_expr_node(tp, index_node);
            return (BashAstNode*)access;
        }
    }

    BashExpansionNode* expansion = (BashExpansionNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_EXPANSION, node, sizeof(BashExpansionNode));

    uint32_t total_children = ts_node_child_count(node);

    // scan all children (named and anonymous) to detect structure
    // typical form: ${ [#] variable_name [operator] [argument] [/] [replacement] }
    bool found_var = false;
    String* op_str = NULL;

    for (uint32_t i = 0; i < total_children; i++) {
        TSNode ch = ts_node_child(node, i);
        const char* ch_type = ts_node_type(ch);
        bool is_named = ts_node_is_named(ch);
        StrView ch_src = bash_node_source(tp, ch);

        if (!is_named) {
            // anonymous tokens: ${, }, operators like :-, ##, %, //, :, etc.
            if (ch_src.length == 2 && ch_src.str[0] == '$' && ch_src.str[1] == '{') continue;
            if (ch_src.length == 1 && ch_src.str[0] == '}') continue;

            // detect operator (comes after variable name)
            if (found_var && !op_str) {
                op_str = name_pool_create_len(tp->name_pool, ch_src.str, ch_src.length);
                if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '-')
                    expansion->expand_type = BASH_EXPAND_DEFAULT;
                else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '=')
                    expansion->expand_type = BASH_EXPAND_ASSIGN_DEFAULT;
                else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '+')
                    expansion->expand_type = BASH_EXPAND_ALT;
                else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '?')
                    expansion->expand_type = BASH_EXPAND_ERROR;
                else if (ch_src.length == 2 && ch_src.str[0] == '#' && ch_src.str[1] == '#')
                    expansion->expand_type = BASH_EXPAND_TRIM_PREFIX_LONG;
                else if (ch_src.length == 1 && ch_src.str[0] == '#')
                    expansion->expand_type = BASH_EXPAND_TRIM_PREFIX;
                else if (ch_src.length == 2 && ch_src.str[0] == '%' && ch_src.str[1] == '%')
                    expansion->expand_type = BASH_EXPAND_TRIM_SUFFIX_LONG;
                else if (ch_src.length == 1 && ch_src.str[0] == '%')
                    expansion->expand_type = BASH_EXPAND_TRIM_SUFFIX;
                else if (ch_src.length == 2 && ch_src.str[0] == '/' && ch_src.str[1] == '/')
                    expansion->expand_type = BASH_EXPAND_REPLACE_ALL;
                else if (ch_src.length == 1 && ch_src.str[0] == '/')
                    expansion->expand_type = BASH_EXPAND_REPLACE;
                else if (ch_src.length == 1 && ch_src.str[0] == ':')
                    expansion->expand_type = BASH_EXPAND_SUBSTRING;
                else if (ch_src.length == 2 && ch_src.str[0] == '^' && ch_src.str[1] == '^')
                    expansion->expand_type = BASH_EXPAND_UPPER_ALL;
                else if (ch_src.length == 1 && ch_src.str[0] == '^')
                    expansion->expand_type = BASH_EXPAND_UPPER_FIRST;
                else if (ch_src.length == 2 && ch_src.str[0] == ',' && ch_src.str[1] == ',')
                    expansion->expand_type = BASH_EXPAND_LOWER_ALL;
                else if (ch_src.length == 1 && ch_src.str[0] == ',')
                    expansion->expand_type = BASH_EXPAND_LOWER_FIRST;
            }
            // skip second / in replacement patterns
            continue;
        }

        // named children: variable_name, word, regex, number
        if (strcmp(ch_type, "variable_name") == 0) {
            expansion->variable = node_text(tp, ch);
            found_var = true;
        } else if (!found_var && strcmp(ch_type, "#") == 0) {
            // for ${#var} the # is before the variable
            expansion->expand_type = BASH_EXPAND_LENGTH;
        } else if (found_var && !expansion->argument) {
            expansion->argument = build_expr_node(tp, ch);
        } else if (found_var && expansion->argument && !expansion->replacement) {
            expansion->replacement = build_expr_node(tp, ch);
        }
    }

    // if no operator was found but we need LENGTH detection from source
    StrView source2 = bash_node_source(tp, node);
    if (source2.length >= 4 && source2.str[0] == '$' && source2.str[1] == '{' &&
        source2.str[2] == '#' && expansion->expand_type == 0) {
        expansion->expand_type = BASH_EXPAND_LENGTH;
        expansion->variable = name_pool_create_len(tp->name_pool, source2.str + 3, source2.length - 4);
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
                    strcmp(item_child_type, "number") == 0 ||
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
        const char* child_type = ts_node_type(child);

        // if a child is negated_command, propagate negation to the pipeline
        // (in bash, ! negates the entire pipeline's exit code)
        if (strcmp(child_type, "negated_command") == 0) {
            pipeline->negated = true;
            uint32_t nc = ts_node_named_child_count(child);
            if (nc > 0) {
                TSNode inner = ts_node_named_child(child, 0);
                BashAstNode* cmd = build_statement(tp, inner);
                if (!cmd) continue;
                if (!pipeline->commands) {
                    pipeline->commands = cmd;
                } else {
                    tail->next = cmd;
                }
                tail = cmd;
                pipeline->command_count++;
            }
            continue;
        }

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
        } else if (strcmp(child_type, "subscript") == 0) {
            // arr[idx]=val — array index assignment
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            TSNode index_node = ts_node_child_by_field_name(child, "index", 5);
            assign->name = node_text(tp, name_node);
            assign->index = build_expr_node(tp, index_node);
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
        // in arithmetic/c_style_for context, simple_expansion may be aliased from
        // c_simple_variable_name (just a bare name like "i", no $ prefix)
        uint32_t nc = ts_node_named_child_count(node);
        if (nc == 0) {
            // bare variable name (c_simple_variable_name aliased to simple_expansion)
            BashArithVariableNode* var = (BashArithVariableNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ARITH_VARIABLE, node, sizeof(BashArithVariableNode));
            var->name = node_text(tp, node);
            return (BashAstNode*)var;
        }
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
        // fix precedence: unary(op, binary(left, binop, right)) →
        // binary(unary(op, left), binop, right) when unary should bind tighter
        if (unary->operand && unary->operand->node_type == BASH_AST_NODE_ARITH_BINARY &&
            (unary->op == BASH_OP_SUB || unary->op == BASH_OP_ADD ||
             unary->op == BASH_OP_BIT_NOT || unary->op == BASH_OP_LOGICAL_NOT)) {
            BashArithBinaryNode* bin = (BashArithBinaryNode*)unary->operand;
            // rewrite: move unary to wrap only the left operand
            BashArithUnaryNode* new_unary = (BashArithUnaryNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ARITH_UNARY, node, sizeof(BashArithUnaryNode));
            new_unary->op = unary->op;
            new_unary->operand = bin->left;
            bin->left = (BashAstNode*)new_unary;
            return (BashAstNode*)bin;
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
    if (strcmp(node_type, "postfix_expression") == 0) {
        // i++ or i-- (tree-sitter c_postfix_expression aliased)
        BashArithAssignNode* assign = (BashArithAssignNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_ASSIGN, node, sizeof(BashArithAssignNode));
        uint32_t ch_count = ts_node_child_count(node);
        for (uint32_t j = 0; j < ch_count; j++) {
            TSNode c = ts_node_child(node, j);
            if (ts_node_is_named(c)) {
                const char* ct = ts_node_type(c);
                if (strcmp(ct, "simple_expansion") == 0) {
                    uint32_t nc = ts_node_named_child_count(c);
                    if (nc > 0) {
                        assign->name = node_text(tp, ts_node_named_child(c, 0));
                    } else {
                        // bare variable name (c_simple_variable_name aliased)
                        assign->name = node_text(tp, c);
                    }
                } else if (strcmp(ct, "variable_name") == 0 || strcmp(ct, "word") == 0) {
                    assign->name = node_text(tp, c);
                }
            } else {
                StrView op_src = bash_node_source(tp, c);
                if (op_src.length == 2 && op_src.str[0] == '+' && op_src.str[1] == '+')
                    assign->op = BASH_OP_INC;
                else if (op_src.length == 2 && op_src.str[0] == '-' && op_src.str[1] == '-')
                    assign->op = BASH_OP_DEC;
            }
        }
        return (BashAstNode*)assign;
    }
    if (strcmp(node_type, "assignment_expression") == 0) {
        // i=0, i+=1, etc. in arithmetic context
        BashArithAssignNode* assign = (BashArithAssignNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_ASSIGN, node, sizeof(BashArithAssignNode));
        uint32_t ch_count = ts_node_child_count(node);
        int operand_idx = 0;
        for (uint32_t j = 0; j < ch_count; j++) {
            TSNode c = ts_node_child(node, j);
            if (ts_node_is_named(c)) {
                if (operand_idx == 0) {
                    // left side: variable
                    const char* ct = ts_node_type(c);
                    if (strcmp(ct, "simple_expansion") == 0) {
                        uint32_t nc = ts_node_named_child_count(c);
                        if (nc > 0) assign->name = node_text(tp, ts_node_named_child(c, 0));
                        else assign->name = node_text(tp, c);
                    } else {
                        assign->name = node_text(tp, c);
                    }
                    operand_idx++;
                } else {
                    // right side: value expression
                    assign->value = build_arith_expression(tp, c);
                }
            } else {
                StrView op_src = bash_node_source(tp, c);
                assign->op = bash_operator_from_string(op_src.str, op_src.length);
            }
        }
        return (BashAstNode*)assign;
    }
    if (strcmp(node_type, "variable_assignment") == 0) {
        // tree-sitter may use variable_assignment for i=0 in c_style_for context
        BashArithAssignNode* assign = (BashArithAssignNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_ASSIGN, node, sizeof(BashArithAssignNode));
        assign->op = BASH_OP_ASSIGN;
        uint32_t ch_count = ts_node_named_child_count(node);
        for (uint32_t j = 0; j < ch_count; j++) {
            TSNode c = ts_node_named_child(node, j);
            const char* ct = ts_node_type(c);
            if (strcmp(ct, "variable_name") == 0) {
                assign->name = node_text(tp, c);
            } else {
                assign->value = build_arith_expression(tp, c);
            }
        }
        return (BashAstNode*)assign;
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

static BashAstNode* build_test_expression(BashTranspiler* tp, TSNode node) {
    const char* type = ts_node_type(node);

    if (strcmp(type, "binary_expression") == 0) {
        // tree-sitter may parse `! $b -gt 5` as `(! $b) -gt 5`.
        // in bash, `!` negates the whole comparison: `! ($b -gt 5)`.
        // detect this and rewrite the AST accordingly.
        TSNode left_node = ts_node_child_by_field_name(node, "left", 4);
        if (!ts_node_is_null(left_node) &&
            strcmp(ts_node_type(left_node), "unary_expression") == 0) {
            TSNode unary_op = ts_node_child_by_field_name(left_node, "operator", 8);
            if (!ts_node_is_null(unary_op)) {
                StrView unary_op_src = bash_node_source(tp, unary_op);
                if (unary_op_src.length == 1 && unary_op_src.str[0] == '!') {
                    // rewrite: unary(!, binary(inner_operand, op, right))
                    TSNode inner_operand = {0};
                    uint32_t un_children = ts_node_named_child_count(left_node);
                    for (uint32_t j = 0; j < un_children; j++) {
                        TSNode uc = ts_node_named_child(left_node, j);
                        if (strcmp(ts_node_type(uc), "test_operator") != 0) {
                            inner_operand = uc;
                            break;
                        }
                    }

                    BashTestBinaryNode* bin = (BashTestBinaryNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_TEST_BINARY, node, sizeof(BashTestBinaryNode));
                    TSNode op_node = ts_node_child_by_field_name(node, "operator", 8);
                    if (!ts_node_is_null(op_node)) {
                        StrView op_src = bash_node_source(tp, op_node);
                        bin->op = bash_test_op_from_string(op_src.str, op_src.length);
                    }
                    if (!ts_node_is_null(inner_operand)) {
                        bin->left = build_test_expression(tp, inner_operand);
                    }
                    TSNode right_node = ts_node_child_by_field_name(node, "right", 5);
                    if (!ts_node_is_null(right_node)) {
                        bin->right = build_test_expression(tp, right_node);
                    }

                    BashTestUnaryNode* unary = (BashTestUnaryNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_TEST_UNARY, left_node, sizeof(BashTestUnaryNode));
                    unary->op = BASH_TEST_NOT;
                    unary->operand = (BashAstNode*)bin;
                    return (BashAstNode*)unary;
                }
            }
        }

        BashTestBinaryNode* bin = (BashTestBinaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_TEST_BINARY, node, sizeof(BashTestBinaryNode));

        TSNode op_node = ts_node_child_by_field_name(node, "operator", 8);
        if (!ts_node_is_null(op_node)) {
            StrView op_src = bash_node_source(tp, op_node);
            bin->op = bash_test_op_from_string(op_src.str, op_src.length);
        }
        TSNode bin_left = ts_node_child_by_field_name(node, "left", 4);
        if (!ts_node_is_null(bin_left)) {
            bin->left = build_test_expression(tp, bin_left);
        }
        TSNode bin_right = ts_node_child_by_field_name(node, "right", 5);
        if (!ts_node_is_null(bin_right)) {
            bin->right = build_test_expression(tp, bin_right);
        }
        return (BashAstNode*)bin;
    } else if (strcmp(type, "unary_expression") == 0) {
        BashTestUnaryNode* unary = (BashTestUnaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_TEST_UNARY, node, sizeof(BashTestUnaryNode));

        TSNode op_node = ts_node_child_by_field_name(node, "operator", 8);
        if (!ts_node_is_null(op_node)) {
            StrView op_src = bash_node_source(tp, op_node);
            unary->op = bash_test_op_from_string(op_src.str, op_src.length);
        }
        uint32_t un_children = ts_node_named_child_count(node);
        for (uint32_t j = 0; j < un_children; j++) {
            TSNode uc = ts_node_named_child(node, j);
            const char* uc_type = ts_node_type(uc);
            if (strcmp(uc_type, "test_operator") != 0) {
                unary->operand = build_test_expression(tp, uc);
                break;
            }
        }
        return (BashAstNode*)unary;
    } else {
        return build_expr_node(tp, node);
    }
}

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
        test->expression = build_test_expression(tp, ts_node_named_child(node, 0));
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
    // detect heredoc / herestring and build appropriate AST
    uint32_t child_count = ts_node_named_child_count(node);

    // check if this is a cat + heredoc/herestring pattern
    BashAstNode* inner_cmd = NULL;
    TSNode heredoc_node = {0};
    bool has_heredoc = false;
    TSNode herestring_node = {0};
    bool has_herestring = false;

    // collect file_redirect nodes
    TSNode file_redirects[16];
    int file_redirect_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "heredoc_redirect") == 0) {
            heredoc_node = child;
            has_heredoc = true;
        } else if (strcmp(child_type, "herestring_redirect") == 0) {
            herestring_node = child;
            has_herestring = true;
        } else if (strcmp(child_type, "file_redirect") == 0) {
            if (file_redirect_count < 16) {
                file_redirects[file_redirect_count++] = child;
            }
        } else {
            inner_cmd = build_statement(tp, child);
        }
    }

    // handle cat <<EOF / cat <<< "str"
    BashCommandNode* cmd = NULL;
    bool is_cat = false;
    if (inner_cmd && inner_cmd->node_type == BASH_AST_NODE_COMMAND) {
        cmd = (BashCommandNode*)inner_cmd;
        if (cmd->name && cmd->name->node_type == BASH_AST_NODE_WORD) {
            BashWordNode* name = (BashWordNode*)cmd->name;
            if (name->text && name->text->len == 3 && memcmp(name->text->chars, "cat", 3) == 0) {
                is_cat = true;
            }
        }
    }

    if (is_cat && has_heredoc) {
        // build heredoc: extract body content from heredoc_redirect
        BashHeredocNode* heredoc = (BashHeredocNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_HEREDOC, heredoc_node, sizeof(BashHeredocNode));

        // check if delimiter is quoted (no expansion)
        uint32_t hrc = ts_node_named_child_count(heredoc_node);
        bool quoted = false;
        for (uint32_t i = 0; i < hrc; i++) {
            TSNode hchild = ts_node_named_child(heredoc_node, i);
            const char* htype = ts_node_type(hchild);
            if (strcmp(htype, "heredoc_start") == 0) {
                StrView delim_src = bash_node_source(tp, hchild);
                // check for quoted delimiter: 'EOF' or "EOF"
                if (delim_src.length > 0 && (delim_src.str[0] == '\'' || delim_src.str[0] == '"')) {
                    quoted = true;
                }
            } else if (strcmp(htype, "heredoc_body") == 0) {
                // build heredoc body: may contain expansions or just literal text
                if (quoted) {
                    // no expansion — just raw text
                    StrView body_src = bash_node_source(tp, hchild);
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, hchild, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool, body_src.str, body_src.length);
                    heredoc->body = (BashAstNode*)word;
                } else {
                    // with expansion — build like a string node
                    BashStringNode* str = (BashStringNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_STRING, hchild, sizeof(BashStringNode));
                    uint32_t body_start = ts_node_start_byte(hchild);
                    uint32_t body_end = ts_node_end_byte(hchild);
                    BashAstNode* parts_tail = NULL;
                    uint32_t pos = body_start;
                    uint32_t named_count = ts_node_named_child_count(hchild);
                    for (uint32_t j = 0; j < named_count; j++) {
                        TSNode bc = ts_node_named_child(hchild, j);
                        const char* bctype = ts_node_type(bc);
                        if (strcmp(bctype, "simple_expansion") == 0 ||
                            strcmp(bctype, "expansion") == 0) {
                            uint32_t dollar_pos = ts_node_start_byte(bc);
                            uint32_t bc_end = ts_node_end_byte(bc);
                            while (dollar_pos < bc_end && tp->source[dollar_pos] != '$') dollar_pos++;
                            if (dollar_pos > pos) {
                                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                                    tp, BASH_AST_NODE_WORD, bc, sizeof(BashWordNode));
                                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, dollar_pos - pos);
                                if (!str->parts) str->parts = (BashAstNode*)gap;
                                else parts_tail->next = (BashAstNode*)gap;
                                parts_tail = (BashAstNode*)gap;
                            }
                            BashAstNode* exp = build_expansion(tp, bc);
                            if (exp) {
                                if (!str->parts) str->parts = exp;
                                else parts_tail->next = exp;
                                parts_tail = exp;
                            }
                            pos = bc_end;
                        } else if (strcmp(bctype, "command_substitution") == 0) {
                            uint32_t cs_start = ts_node_start_byte(bc);
                            if (cs_start > pos) {
                                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                                    tp, BASH_AST_NODE_WORD, bc, sizeof(BashWordNode));
                                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, cs_start - pos);
                                if (!str->parts) str->parts = (BashAstNode*)gap;
                                else parts_tail->next = (BashAstNode*)gap;
                                parts_tail = (BashAstNode*)gap;
                            }
                            BashAstNode* csub = build_command_substitution(tp, bc);
                            if (csub) {
                                if (!str->parts) str->parts = csub;
                                else parts_tail->next = csub;
                                parts_tail = csub;
                            }
                            pos = ts_node_end_byte(bc);
                        }
                    }
                    // trailing literal
                    if (pos < body_end) {
                        BashWordNode* tail = (BashWordNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_WORD, hchild, sizeof(BashWordNode));
                        tail->text = name_pool_create_len(tp->name_pool, tp->source + pos, body_end - pos);
                        if (!str->parts) str->parts = (BashAstNode*)tail;
                        else parts_tail->next = (BashAstNode*)tail;
                    }
                    heredoc->body = (BashAstNode*)str;
                }
            }
        }
        heredoc->expand = !quoted;
        return (BashAstNode*)heredoc;
    }

    if (is_cat && has_herestring) {
        // cat <<< "string" — extract the string argument
        BashHeredocNode* heredoc = (BashHeredocNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_HERESTRING, herestring_node, sizeof(BashHeredocNode));
        uint32_t hsc = ts_node_named_child_count(herestring_node);
        for (uint32_t i = 0; i < hsc; i++) {
            TSNode hchild = ts_node_named_child(herestring_node, i);
            const char* htype = ts_node_type(hchild);
            if (strcmp(htype, "string") == 0) {
                heredoc->body = build_string_node(tp, hchild);
            } else if (strcmp(htype, "word") == 0) {
                heredoc->body = build_word(tp, hchild);
            } else if (strcmp(htype, "concatenation") == 0) {
                heredoc->body = build_concatenation(tp, hchild);
            }
        }
        heredoc->expand = true;
        return (BashAstNode*)heredoc;
    }

    // attach file_redirect nodes to the command
    if (file_redirect_count > 0 && inner_cmd && inner_cmd->node_type == BASH_AST_NODE_COMMAND) {
        BashCommandNode* cmd = (BashCommandNode*)inner_cmd;
        BashAstNode* redir_tail = NULL;
        for (int i = 0; i < file_redirect_count; i++) {
            TSNode fr = file_redirects[i];
            BashRedirectNode* redir = (BashRedirectNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECT, fr, sizeof(BashRedirectNode));
            redir->fd = 1; // default: stdout
            redir->mode = BASH_REDIR_WRITE; // default: >

            // detect redirect operator and optional fd from anonymous children
            uint32_t total_children = ts_node_child_count(fr);
            for (uint32_t j = 0; j < total_children; j++) {
                TSNode ch = ts_node_child(fr, j);
                const char* ch_type = ts_node_type(ch);
                if (strcmp(ch_type, "file_descriptor") == 0) {
                    StrView fd_src = bash_node_source(tp, ch);
                    redir->fd = (fd_src.length > 0) ? atoi(fd_src.str) : 1;
                } else if (!ts_node_is_named(ch)) {
                    // anonymous child = redirect operator
                    StrView op = bash_node_source(tp, ch);
                    if (op.length == 1 && op.str[0] == '>') {
                        redir->mode = BASH_REDIR_WRITE;
                    } else if (op.length == 2 && op.str[0] == '>' && op.str[1] == '>') {
                        redir->mode = BASH_REDIR_APPEND;
                    } else if (op.length == 1 && op.str[0] == '<') {
                        redir->mode = BASH_REDIR_READ;
                        redir->fd = 0;
                    } else if (op.length == 2 && op.str[0] == '>' && op.str[1] == '&') {
                        redir->mode = BASH_REDIR_DUP;
                    } else if (op.length == 2 && op.str[0] == '&' && op.str[1] == '>') {
                        redir->mode = BASH_REDIR_WRITE;
                        redir->fd = -1; // both stdout and stderr
                    } else if (op.length == 2 && op.str[0] == '>' && op.str[1] == '|') {
                        redir->mode = BASH_REDIR_WRITE; // >| same as > for us
                    }
                }
            }

            // destination is the named child that is not file_descriptor
            uint32_t named_count = ts_node_named_child_count(fr);
            for (uint32_t j = 0; j < named_count; j++) {
                TSNode dest_child = ts_node_named_child(fr, j);
                const char* dtype = ts_node_type(dest_child);
                if (strcmp(dtype, "file_descriptor") == 0) continue;
                // build destination node based on its type
                if (strcmp(dtype, "word") == 0 || strcmp(dtype, "number") == 0) {
                    redir->target = build_word(tp, dest_child);
                } else if (strcmp(dtype, "string") == 0) {
                    redir->target = build_string_node(tp, dest_child);
                } else if (strcmp(dtype, "concatenation") == 0) {
                    redir->target = build_concatenation(tp, dest_child);
                } else if (strcmp(dtype, "simple_expansion") == 0 ||
                           strcmp(dtype, "expansion") == 0) {
                    redir->target = build_expansion(tp, dest_child);
                } else {
                    redir->target = build_word(tp, dest_child);
                }
                break;
            }

            if (!cmd->redirects) {
                cmd->redirects = (BashAstNode*)redir;
            } else {
                redir_tail->next = (BashAstNode*)redir;
            }
            redir_tail = (BashAstNode*)redir;
        }
    }

    return inner_cmd;
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
