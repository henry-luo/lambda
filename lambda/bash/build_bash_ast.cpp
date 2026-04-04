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
#include "../../lib/strbuf.h"
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
static BashAstNode* build_ansi_c_string(BashTranspiler* tp, TSNode node);
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
static BashAstNode* scan_text_for_dollar_vars(BashTranspiler* tp, String* text, TSNode node);
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
static BashAstNode* build_expr_node(BashTranspiler* tp, TSNode node);
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
    const char* src = tp->source;
    // strip leading whitespace (tree-sitter may include preceding newlines in word spans)
    while (start < end && (src[start] == ' ' || src[start] == '\t' || src[start] == '\n' || src[start] == '\r')) {
        start++;
    }
    // strip trailing whitespace
    while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\n' || src[end - 1] == '\r')) {
        end--;
    }
    size_t len = end - start;
    return name_pool_create_len(tp->name_pool, src + start, len);
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
    log_debug("bash: unknown operator: %.*s", (int)len, op_str);
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
        if (memcmp(op_str, "-nt", 3) == 0) return BASH_TEST_NT;
        if (memcmp(op_str, "-ot", 3) == 0) return BASH_TEST_OT;
        if (memcmp(op_str, "-ef", 3) == 0) return BASH_TEST_EF;
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
    log_debug("bash: unknown test operator: %.*s", (int)len, op_str);
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
    if (strcmp(type, "variable_assignments") == 0) {
        // multiple assignments on same line: a=1 b=2 (no command)
        BashBlockNode* block = (BashBlockNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_BLOCK, node, sizeof(BashBlockNode));
        uint32_t nc = ts_node_named_child_count(node);
        BashAstNode* tail = NULL;
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i);
            BashAstNode* assign = build_assignment(tp, child);
            if (!assign) continue;
            if (!block->statements) { block->statements = assign; }
            else { tail->next = assign; }
            tail = assign;
        }
        return (BashAstNode*)block;
    }
    if (strcmp(type, "declaration_command") == 0) return build_declaration(tp, node);
    if (strcmp(type, "redirected_statement") == 0) return build_redirected(tp, node);
    if (strcmp(type, "test_command") == 0) return build_test_command(tp, node);
    // ((expr)) as a standalone statement: tree-sitter-bash uses arithmetic_expansion
    if (strcmp(type, "arithmetic_expansion") == 0) return build_arith_expression(tp, node);
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
            if (strcmp(child_type, "word") == 0 ||
                strcmp(child_type, "variable_name") == 0) arg = build_word(tp, child);
            else if (strcmp(child_type, "raw_string") == 0) arg = build_raw_string(tp, child);
            else if (strcmp(child_type, "ansi_c_string") == 0) arg = build_ansi_c_string(tp, child);
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
        // standalone `!` — negate current $? with no command
        BashPipelineNode* pipeline = (BashPipelineNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_PIPELINE, node, sizeof(BashPipelineNode));
        pipeline->commands = NULL;
        pipeline->command_count = 0;
        pipeline->negated = true;
        return (BashAstNode*)pipeline;
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
    uint32_t prev_arg_end_byte = 0;  // track end byte of previous argument for merge detection
    cmd->arg_count = 0;
    int last_assign_line = -1;   // track last prefix-assignment line
    int cmd_name_line = -1;      // track command_name line

    // check for herestring_redirect child (cat <<< "str" parsed as command, not redirected_statement)
    TSNode herestring_child = {0};
    bool has_herestring_child = false;
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
                            // heredoc body words are in a quoted-like context: no tilde expansion
                            ((BashWordNode*)heredoc->body)->no_backslash_escape = true;
                        } else if (strcmp(htype, "concatenation") == 0) {
                            heredoc->body = build_concatenation(tp, hchild);
                        } else if (strcmp(htype, "raw_string") == 0) {
                            heredoc->body = build_raw_string(tp, hchild);
                        }
                    }
                    heredoc->expand = true;
                    return (BashAstNode*)heredoc;
                }
                // non-cat: remember the herestring for wrapping later
                herestring_child = child;
                has_herestring_child = true;
            }
        }
    }

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        StrView ch_src = bash_node_source(tp, child);
        TSPoint ch_start = ts_node_start_point(child);
        TSPoint ch_end = ts_node_end_point(child);
        uint32_t ch_start_byte = ts_node_start_byte(child);
        uint32_t ch_end_byte = ts_node_end_byte(child);

        // skip redirect children – they are handled separately
        if (strcmp(child_type, "herestring_redirect") == 0 ||
            strcmp(child_type, "file_redirect") == 0 ||
            strcmp(child_type, "heredoc_redirect") == 0) {
            continue;
        }

        BashAstNode* child_ast = NULL;

        if (strcmp(child_type, "word") == 0 ||
            strcmp(child_type, "number") == 0) {
            child_ast = build_word(tp, child);
        } else if (strcmp(child_type, "string") == 0) {
            child_ast = build_string_node(tp, child);
        } else if (strcmp(child_type, "raw_string") == 0) {
            child_ast = build_raw_string(tp, child);
        } else if (strcmp(child_type, "ansi_c_string") == 0) {
            child_ast = build_ansi_c_string(tp, child);
        } else if (strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0) {
            child_ast = build_expansion(tp, child);
        } else if (strcmp(child_type, "command_substitution") == 0) {
            child_ast = build_command_substitution(tp, child);
        } else if (strcmp(child_type, "concatenation") == 0) {
            child_ast = build_concatenation(tp, child);
        } else if (strcmp(child_type, "arithmetic_expansion") == 0) {
            child_ast = build_arith_expression(tp, child);
        } else if (strcmp(child_type, "command_name") == 0) {
            cmd_name_line = (int)ts_node_start_point(child).row;
            // command_name is a wrapper node: dispatch on its first named child
            // first, check if the source text looks like VAR=VALUE (misparse by tree-sitter)
            StrView cmd_src = bash_node_source(tp, child);
            int eq_pos = -1;
            bool looks_like_assign = false;
            for (size_t k = 0; k < cmd_src.length; k++) {
                char c = cmd_src.str[k];
                if (c == '=') { eq_pos = (int)k; looks_like_assign = (eq_pos > 0); break; }
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_')) break;
            }
            if (looks_like_assign) {
                // treat as variable assignment: name = chars before '=', value = chars after '='
                BashAssignmentNode* assign = (BashAssignmentNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_ASSIGNMENT, child, sizeof(BashAssignmentNode));
                assign->name = name_pool_create_len(tp->name_pool, cmd_src.str, eq_pos);
                const char* val_start = cmd_src.str + eq_pos + 1;
                size_t val_len = cmd_src.length - eq_pos - 1;
                if (val_len > 0) {
                    BashWordNode* val_word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                    val_word->text = name_pool_create_len(tp->name_pool, val_start, val_len);
                    assign->value = (BashAstNode*)val_word;
                }
                // add to prefix assignments
                ((BashAstNode*)assign)->next = cmd->assignments;
                cmd->assignments = (BashAstNode*)assign;
                continue;
            }
            if (ts_node_named_child_count(child) > 0) {
                child_ast = build_expr_node(tp, ts_node_named_child(child, 0));
            } else {
                child_ast = build_word(tp, child);
            }
        } else if (strcmp(child_type, "variable_assignment") == 0) {
            // prefix assignment: VAR=value cmd
            last_assign_line = (int)ts_node_start_point(child).row;
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

        // tree-sitter sometimes splits a single argument into multiple adjacent nodes
        // (e.g., "$a/$b/file" → argument "$a/" + argument "$b/file").
        // detect this by checking if this child starts exactly where the previous one ended
        // (no whitespace gap), or with only a "$" in between (anonymous token lost by tree-sitter).
        if (args_tail && prev_arg_end_byte > 0) {
            bool should_merge = false;
            bool has_dollar_gap = false;
            if (ch_start_byte == prev_arg_end_byte) {
                should_merge = true;
            } else if (ch_start_byte == prev_arg_end_byte + 1 && tp->source &&
                       prev_arg_end_byte < (uint32_t)tp->source_length &&
                       tp->source[prev_arg_end_byte] == '$') {
                // the gap is a single '$' that tree-sitter treated as anonymous token
                should_merge = true;
                has_dollar_gap = true;
            }
            if (should_merge) {
                // check if prevarg's trailing anonymous '$' or a gap '$' needs handling
                bool needs_dollar_prefix = has_dollar_gap;
                if (!has_dollar_gap && ch_start_byte > 0 && tp->source &&
                    ch_start_byte <= (uint32_t)tp->source_length &&
                    tp->source[ch_start_byte - 1] == '$') {
                    // anonymous '$' inside the previous node (trailing)
                    needs_dollar_prefix = true;
                }
                if (needs_dollar_prefix) {
                    // reconstruct the text with the '$' prefix and scan for variable refs
                    uint32_t text_start = has_dollar_gap ? prev_arg_end_byte : (ch_start_byte - 1);
                    String* full_text = name_pool_create_len(tp->name_pool,
                        tp->source + text_start,
                        ch_end_byte - text_start);
                    BashAstNode* split_result = scan_text_for_dollar_vars(tp, full_text, child);
                    if (split_result) {
                        child_ast = split_result;
                    } else {
                        BashWordNode* synth = (BashWordNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                        synth->text = full_text;
                        child_ast = (BashAstNode*)synth;
                    }
                }
                // merge: wrap args_tail and child_ast into a concatenation
            BashConcatNode* concat = NULL;
            if (args_tail->node_type == BASH_AST_NODE_CONCATENATION) {
                // previous arg is already a concat — just append to it
                concat = (BashConcatNode*)args_tail;
                BashAstNode* tail = concat->parts;
                while (tail && tail->next) tail = tail->next;
                // if child_ast is a concat, flatten its parts into this concat
                if (child_ast->node_type == BASH_AST_NODE_CONCATENATION) {
                    BashAstNode* parts = ((BashConcatNode*)child_ast)->parts;
                    if (tail) tail->next = parts;
                    else concat->parts = parts;
                } else {
                    child_ast->next = NULL;
                    if (tail) tail->next = child_ast;
                    else concat->parts = child_ast;
                }
            } else {
                // create new concat node wrapping both
                concat = (BashConcatNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_CONCATENATION, child, sizeof(BashConcatNode));
                BashAstNode* prev = args_tail;
                // detach prev from the arg list (it's being absorbed into concat)
                prev->next = child_ast;
                child_ast->next = NULL;
                concat->parts = prev;
                // replace args_tail in the list with the concat
                if (cmd->args == args_tail) {
                    cmd->args = (BashAstNode*)concat;
                } else {
                    // find the node before args_tail
                    BashAstNode* p = cmd->args;
                    while (p && p->next != args_tail) p = p->next;
                    if (p) p->next = (BashAstNode*)concat;
                }
            }
            args_tail = (BashAstNode*)concat;
            prev_arg_end_byte = ch_end_byte;
            continue;
            }
        }

        // check if the name node also needs merging (rare: command_name and first arg adjacent)
        if (!cmd->name) {
            cmd->name = child_ast;
        } else if (cmd->name && !cmd->args && cmd->name != child_ast &&
                   ch_start_byte == prev_arg_end_byte) {
            // merge name + this child into a concatenation to form the real name
            BashConcatNode* concat = (BashConcatNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_CONCATENATION, child, sizeof(BashConcatNode));
            cmd->name->next = child_ast;
            child_ast->next = NULL;
            concat->parts = cmd->name;
            cmd->name = (BashAstNode*)concat;
            prev_arg_end_byte = ch_end_byte;
            continue;
        } else {
            if (!cmd->args) {
                cmd->args = child_ast;
            } else {
                args_tail->next = child_ast;
            }
            args_tail = child_ast;
            cmd->arg_count++;
        }
        prev_arg_end_byte = ch_end_byte;
    }

    // split: if all prefix assignments are on an earlier line than the command_name,
    // they are standalone assignments, not temporary env vars for the command.
    // tree-sitter sometimes merges "a=one b=two\necho x" into a single command node.
    if (cmd->assignments && cmd->name && last_assign_line >= 0 && cmd_name_line > last_assign_line) {
        // return a BLOCK: standalone assignments + the command (without assignments)
        BashBlockNode* block = (BashBlockNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_BLOCK, node, sizeof(BashBlockNode));
        // reverse the assignment list (they were prepended) so they execute in source order
        BashAstNode* rev = NULL;
        BashAstNode* cur = cmd->assignments;
        while (cur) {
            BashAstNode* next = cur->next;
            cur->next = rev;
            rev = cur;
            cur = next;
        }
        block->statements = rev;
        // find the tail of the reversed assignment list
        BashAstNode* tail = rev;
        while (tail->next) tail = tail->next;
        // detach assignments from the command
        cmd->assignments = NULL;
        // check if the command name is a reserved keyword (tree-sitter error recovery
        // sometimes folds "a=1 b=2\nfor x in..." into a single command node where
        // "for" becomes the command name — the loop body is mangled, so just drop it)
        bool is_reserved_kw = false;
        if (cmd->name && cmd->name->node_type == BASH_AST_NODE_WORD) {
            BashWordNode* wn = (BashWordNode*)cmd->name;
            const char* kw = (wn->text && wn->text->chars) ? wn->text->chars : "";
            is_reserved_kw = !strcmp(kw,"for") || !strcmp(kw,"while") || !strcmp(kw,"until")
                          || !strcmp(kw,"if") || !strcmp(kw,"do") || !strcmp(kw,"done")
                          || !strcmp(kw,"fi") || !strcmp(kw,"esac") || !strcmp(kw,"then")
                          || !strcmp(kw,"elif") || !strcmp(kw,"else") || !strcmp(kw,"case")
                          || !strcmp(kw,"select") || !strcmp(kw,"in");
        }
        if (!is_reserved_kw) {
            tail->next = (BashAstNode*)cmd;
            ((BashAstNode*)cmd)->next = NULL;
        }
        return (BashAstNode*)block;
    }

    // if we detected a herestring_redirect for a non-cat command, wrap with REDIRECTED
    if (has_herestring_child) {
        BashAstNode* hs_body = NULL;
        uint32_t hsc = ts_node_named_child_count(herestring_child);
        for (uint32_t j = 0; j < hsc; j++) {
            TSNode hchild = ts_node_named_child(herestring_child, j);
            const char* htype = ts_node_type(hchild);
            if (strcmp(htype, "string") == 0) {
                hs_body = build_string_node(tp, hchild);
            } else if (strcmp(htype, "word") == 0) {
                hs_body = build_word(tp, hchild);
            } else if (strcmp(htype, "concatenation") == 0) {
                hs_body = build_concatenation(tp, hchild);
            } else if (strcmp(htype, "simple_expansion") == 0 ||
                       strcmp(htype, "expansion") == 0) {
                hs_body = build_expansion(tp, hchild);
            } else if (strcmp(htype, "raw_string") == 0) {
                hs_body = build_raw_string(tp, hchild);
            }
        }
        if (hs_body) {
            BashRedirectNode* redir = (BashRedirectNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECT, herestring_child, sizeof(BashRedirectNode));
            redir->fd = 0;
            redir->mode = BASH_REDIR_HERESTRING;
            redir->target = hs_body;

            BashRedirectedNode* wrapper = (BashRedirectedNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECTED, node, sizeof(BashRedirectedNode));
            wrapper->inner = (BashAstNode*)cmd;
            wrapper->redirects = (BashAstNode*)redir;
            return (BashAstNode*)wrapper;
        }
    }

    return (BashAstNode*)cmd;
}

// scan text for embedded $var patterns (e.g. "$b/file" where tree-sitter missed the $)
// returns a BashConcatNode if $vars found, or NULL if no splitting needed
static BashAstNode* scan_text_for_dollar_vars(BashTranspiler* tp, String* text, TSNode node) {
    if (!text || text->len < 2) return NULL;
    bool has_dollar = false;
    for (int k = 0; k < text->len; k++) {
        if (text->chars[k] == '$') { has_dollar = true; break; }
    }
    if (!has_dollar) return NULL;

    BashConcatNode* concat = NULL;
    BashAstNode* parts_tail = NULL;
    int i = 0;
    int literal_start = 0;
    bool did_split = false;

    while (i < text->len) {
        if (text->chars[i] == '\\' && i + 1 < text->len) { i += 2; continue; }
        if (text->chars[i] == '$' && i + 1 < text->len) {
            char next = text->chars[i + 1];
            bool is_var = false;
            int var_name_end = i + 1;

            if (next == '{') {
                int depth = 1; int j = i + 2;
                while (j < text->len && depth > 0) {
                    if (text->chars[j] == '{') depth++;
                    else if (text->chars[j] == '}') depth--;
                    j++;
                }
                if (depth == 0) { is_var = true; var_name_end = j; }
            } else if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next == '_') {
                int j = i + 1;
                while (j < text->len && ((text->chars[j] >= 'a' && text->chars[j] <= 'z') ||
                       (text->chars[j] >= 'A' && text->chars[j] <= 'Z') ||
                       (text->chars[j] >= '0' && text->chars[j] <= '9') || text->chars[j] == '_')) j++;
                is_var = true; var_name_end = j;
            } else if (next == '?' || next == '#' || next == '@' || next == '*' ||
                       next == '$' || next == '!' || next == '-' ||
                       (next >= '0' && next <= '9')) {
                is_var = true; var_name_end = i + 2;
            }

            if (is_var) {
                did_split = true;
                if (!concat) concat = (BashConcatNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_CONCATENATION, node, sizeof(BashConcatNode));
                // emit literal part before the variable
                if (i > literal_start) {
                    BashWordNode* lit = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
                    lit->text = name_pool_create_len(tp->name_pool, text->chars + literal_start, i - literal_start);
                    if (!concat->parts) concat->parts = (BashAstNode*)lit;
                    else parts_tail->next = (BashAstNode*)lit;
                    parts_tail = (BashAstNode*)lit;
                }
                // emit variable reference
                if (text->chars[i + 1] == '{') {
                    int name_s = i + 2;
                    int name_e = var_name_end - 1;
                    BashVarRefNode* ref = (BashVarRefNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_VARIABLE_REF, node, sizeof(BashVarRefNode));
                    ref->name = name_pool_create_len(tp->name_pool, text->chars + name_s, name_e - name_s);
                    if (!concat->parts) concat->parts = (BashAstNode*)ref;
                    else parts_tail->next = (BashAstNode*)ref;
                    parts_tail = (BashAstNode*)ref;
                } else {
                    int name_s = i + 1;
                    int name_len = var_name_end - name_s;
                    if (name_len == 1 && (text->chars[name_s] == '?' || text->chars[name_s] == '#' ||
                        text->chars[name_s] == '@' || text->chars[name_s] == '*' ||
                        text->chars[name_s] == '$' || text->chars[name_s] == '!' ||
                        text->chars[name_s] == '-' ||
                        (text->chars[name_s] >= '0' && text->chars[name_s] <= '9'))) {
                        BashSpecialVarNode* sv = (BashSpecialVarNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_SPECIAL_VARIABLE, node, sizeof(BashSpecialVarNode));
                        switch (text->chars[name_s]) {
                        case '?': sv->special_id = BASH_SPECIAL_QUESTION; break;
                        case '#': sv->special_id = BASH_SPECIAL_HASH; break;
                        case '@': sv->special_id = BASH_SPECIAL_AT; break;
                        case '*': sv->special_id = BASH_SPECIAL_STAR; break;
                        case '$': sv->special_id = BASH_SPECIAL_DOLLAR; break;
                        case '!': sv->special_id = BASH_SPECIAL_BANG; break;
                        case '-': sv->special_id = BASH_SPECIAL_DASH; break;
                        default:
                            if (text->chars[name_s] >= '0' && text->chars[name_s] <= '9')
                                sv->special_id = BASH_SPECIAL_ZERO + (text->chars[name_s] - '0');
                            else sv->special_id = BASH_SPECIAL_QUESTION;
                            break;
                        }
                        if (!concat->parts) concat->parts = (BashAstNode*)sv;
                        else parts_tail->next = (BashAstNode*)sv;
                        parts_tail = (BashAstNode*)sv;
                    } else {
                        BashVarRefNode* ref = (BashVarRefNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_VARIABLE_REF, node, sizeof(BashVarRefNode));
                        ref->name = name_pool_create_len(tp->name_pool, text->chars + name_s, name_len);
                        if (!concat->parts) concat->parts = (BashAstNode*)ref;
                        else parts_tail->next = (BashAstNode*)ref;
                        parts_tail = (BashAstNode*)ref;
                    }
                }
                literal_start = var_name_end;
                i = var_name_end;
                continue;
            }
        }
        i++;
    }

    if (did_split && concat) {
        if (literal_start < text->len) {
            BashWordNode* lit = (BashWordNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
            lit->text = name_pool_create_len(tp->name_pool, text->chars + literal_start, text->len - literal_start);
            if (!concat->parts) concat->parts = (BashAstNode*)lit;
            else parts_tail->next = (BashAstNode*)lit;
        }
        return (BashAstNode*)concat;
    }
    return NULL;
}

static BashAstNode* build_word(BashTranspiler* tp, TSNode node) {
    String* text = node_text(tp, node);
    
    // detect if the word contains embedded $var patterns that tree-sitter missed
    if (text && text->len > 1) {
        BashAstNode* result = scan_text_for_dollar_vars(tp, text, node);
        if (result) return result;
    }

    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
    word->text = text;
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
    } else if (strcmp(type, "ansi_c_string") == 0) {
        return build_ansi_c_string(tp, node);
    } else if (strcmp(type, "command_substitution") == 0) {
        return build_command_substitution(tp, node);
    } else if (strcmp(type, "concatenation") == 0) {
        return build_concatenation(tp, node);
    }
    return build_word(tp, node);
}

// mark expansion argument words as quoted (no tilde expansion)
// called when an expansion is inside a double-quoted string
static void mark_expansion_args_quoted(BashAstNode* node) {
    if (!node || node->node_type != BASH_AST_NODE_EXPANSION) return;
    BashExpansionNode* exp = (BashExpansionNode*)node;
    if (exp->argument && exp->argument->node_type == BASH_AST_NODE_WORD) {
        ((BashWordNode*)exp->argument)->no_backslash_escape = true;
    }
    if (exp->replacement && exp->replacement->node_type == BASH_AST_NODE_WORD) {
        ((BashWordNode*)exp->replacement)->no_backslash_escape = true;
    }
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
            literal->no_backslash_escape = true;
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
                gap->no_backslash_escape = true;
                append_part((BashAstNode*)gap);
            }
            BashAstNode* exp_node = build_expansion(tp, child);
            mark_expansion_args_quoted(exp_node);
            append_part(exp_node);
            pos = child_end_byte;
        } else if (strcmp(child_type, "command_substitution") == 0) {
            uint32_t cs_start = ts_node_start_byte(child);
            if (cs_start > pos) {
                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, cs_start - pos);
                gap->no_backslash_escape = true;
                append_part((BashAstNode*)gap);
            }
            append_part(build_command_substitution(tp, child));
            pos = ts_node_end_byte(child);
        } else if (strcmp(child_type, "arithmetic_expansion") == 0) {
            uint32_t ae_start = ts_node_start_byte(child);
            if (ae_start > pos) {
                BashWordNode* gap = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                gap->text = name_pool_create_len(tp->name_pool, tp->source + pos, ae_start - pos);
                gap->no_backslash_escape = true;
                append_part((BashAstNode*)gap);
            }
            append_part(build_arith_expression(tp, child));
            pos = ts_node_end_byte(child);
        } else if (strcmp(child_type, "string_content") == 0) {
            uint32_t sc_end = ts_node_end_byte(child);
            // use pos (not sc_start) so gaps like newlines between string_content nodes are included
            if (sc_end > pos) {
                // process double-quote escape sequences: \$ \` \\ \" \newline
                const char* src = tp->source + pos;
                uint32_t src_len = sc_end - pos;
                StrBuf* sb = strbuf_new_cap((int)src_len + 1);
                for (uint32_t ei = 0; ei < src_len; ei++) {
                    char c = src[ei];
                    if (c == '\\' && ei + 1 < src_len) {
                        char next = src[ei + 1];
                        if (next == '$' || next == '`' || next == '\\' || next == '"' || next == '\n') {
                            if (next != '\n') strbuf_append_char(sb, next);
                            ei++; // skip escaped char
                            continue;
                        }
                    }
                    strbuf_append_char(sb, c);
                }
                BashWordNode* literal = (BashWordNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                literal->text = name_pool_create_len(tp->name_pool, sb->str, (int)sb->length);
                literal->no_backslash_escape = true;
                strbuf_free(sb);
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
                literal->no_backslash_escape = true;
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
        tail->no_backslash_escape = true;
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

static BashAstNode* build_ansi_c_string(BashTranspiler* tp, TSNode node) {
    // ANSI-C quoted string: $'...'
    BashRawStringNode* raw = (BashRawStringNode*)alloc_bash_ast_node(
        tp, BASH_AST_NODE_RAW_STRING, node, sizeof(BashRawStringNode));

    uint32_t start = ts_node_start_byte(node) + 2;  // skip $'
    uint32_t end = ts_node_end_byte(node) - 1;      // skip trailing '
    if (end <= start) {
        raw->text = name_pool_create_len(tp->name_pool, "", 0);
        return (BashAstNode*)raw;
    }

    StrBuf* sb = strbuf_new_cap((int)(end - start) + 1);
    const char* s = tp->source + start;
    uint32_t i = 0;
    while (start + i < end) {
        char c = s[i++];
        if (c != '\\' || start + i >= end) {
            strbuf_append_char(sb, c);
            continue;
        }

        char esc = s[i++];
        switch (esc) {
        case 'a': strbuf_append_char(sb, '\a'); break;
        case 'b': strbuf_append_char(sb, '\b'); break;
        case 'e': case 'E': strbuf_append_char(sb, 27); break;
        case 'f': strbuf_append_char(sb, '\f'); break;
        case 'n': strbuf_append_char(sb, '\n'); break;
        case 'r': strbuf_append_char(sb, '\r'); break;
        case 't': strbuf_append_char(sb, '\t'); break;
        case 'v': strbuf_append_char(sb, '\v'); break;
        case '\\': strbuf_append_char(sb, '\\'); break;
        case '\'': strbuf_append_char(sb, '\''); break;
        case '"': strbuf_append_char(sb, '"'); break;
        case 'c': {
            if (start + i < end) {
                unsigned char ctrl = (unsigned char)s[i++];
                strbuf_append_char(sb, (char)(ctrl & 0x1f));
            }
            break;
        }
        case 'x': {
            int value = 0;
            bool have_digits = false;
            if (start + i < end && s[i] == '{') {
                i++;
                while (start + i < end && s[i] != '}') {
                    char h = s[i];
                    int digit = -1;
                    if (h >= '0' && h <= '9') digit = h - '0';
                    else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                    else break;
                    value = value * 16 + digit;
                    have_digits = true;
                    i++;
                }
                if (start + i < end && s[i] == '}') {
                    i++;
                }
                if (!have_digits) {
                    // bash treats malformed \x{...} with no hex digits as terminating the string
                    i = end - start;
                    break;
                }
            } else {
                for (int j = 0; j < 2 && start + i < end; j++) {
                    char h = s[i];
                    int digit = -1;
                    if (h >= '0' && h <= '9') digit = h - '0';
                    else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                    else break;
                    value = value * 16 + digit;
                    have_digits = true;
                    i++;
                }
            }
            if (have_digits) strbuf_append_char(sb, (char)value);
            break;
        }
        case 'u':
        case 'U': {
            int max_digits = (esc == 'u') ? 4 : 8;
            int value = 0;
            bool have_digits = false;
            for (int j = 0; j < max_digits && start + i < end; j++) {
                char h = s[i];
                int digit = -1;
                if (h >= '0' && h <= '9') digit = h - '0';
                else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                else break;
                value = value * 16 + digit;
                have_digits = true;
                i++;
            }
            if (have_digits) {
                if (value <= 0x7f) strbuf_append_char(sb, (char)value);
                else if (value <= 0x7ff) {
                    strbuf_append_char(sb, (char)(0xc0 | ((value >> 6) & 0x1f)));
                    strbuf_append_char(sb, (char)(0x80 | (value & 0x3f)));
                } else if (value <= 0xffff) {
                    strbuf_append_char(sb, (char)(0xe0 | ((value >> 12) & 0x0f)));
                    strbuf_append_char(sb, (char)(0x80 | ((value >> 6) & 0x3f)));
                    strbuf_append_char(sb, (char)(0x80 | (value & 0x3f)));
                } else {
                    strbuf_append_char(sb, (char)(0xf0 | ((value >> 18) & 0x07)));
                    strbuf_append_char(sb, (char)(0x80 | ((value >> 12) & 0x3f)));
                    strbuf_append_char(sb, (char)(0x80 | ((value >> 6) & 0x3f)));
                    strbuf_append_char(sb, (char)(0x80 | (value & 0x3f)));
                }
            }
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            int value = esc - '0';
            for (int j = 0; j < 2 && start + i < end && s[i] >= '0' && s[i] <= '7'; j++) {
                value = value * 8 + (s[i++] - '0');
            }
            strbuf_append_char(sb, (char)value);
            break;
        }
        default:
            strbuf_append_char(sb, esc);
            break;
        }
    }

    raw->text = name_pool_create_len(tp->name_pool, sb->str, sb->length);
    strbuf_free(sb);
    return (BashAstNode*)raw;
}

static BashAstNode* build_concatenation(BashTranspiler* tp, TSNode node) {
    // check if the entire concatenation is a brace expansion pattern
    // e.g., {a,b,c}, ff{a,b,c}, pre{a..z}post, /usr/{lib,bin}
    StrView src = bash_node_source(tp, node);
    if (src.length >= 3) {
        // look for unescaped { with matching } containing comma or ..
        int depth = 0;
        bool found_brace = false;
        for (size_t i = 0; i < src.length; i++) {
            if (src.str[i] == '\\' && i + 1 < src.length) { i++; continue; }
            if (src.str[i] == '$' && i + 1 < src.length && src.str[i + 1] == '{') {
                int d = 1; i += 2;
                while (i < src.length && d > 0) {
                    if (src.str[i] == '{') d++;
                    else if (src.str[i] == '}') d--;
                    i++;
                }
                i--; continue;
            }
            if (src.str[i] == '{') {
                depth++;
            } else if (src.str[i] == '}') {
                depth--;
                if (depth == 0 && found_brace) {
                    // verify this is a valid brace pattern
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, node, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool, src.str, (int)src.length);
                    return (BashAstNode*)word;
                }
            } else if (depth == 1) {
                if (src.str[i] == ',') found_brace = true;
                if (src.str[i] == '.' && i + 1 < src.length && src.str[i + 1] == '.') found_brace = true;
            }
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
        } else if (strcmp(child_type, "ansi_c_string") == 0) {
            part = build_ansi_c_string(tp, child);
        } else if (strcmp(child_type, "command_substitution") == 0) {
            part = build_command_substitution(tp, child);
        } else if (strcmp(child_type, "arithmetic_expansion") == 0) {
            part = build_arith_expression(tp, child);
        } else if (strcmp(child_type, "brace_expression") == 0) {
            // brace expansion inside concatenation: e.g., ff{a,b,c} → "ffa", "ffb", "ffc"
            // store as WORD node with literal brace text so transpiler can detect it
            part = build_word(tp, child);
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
        bool has_bang_prefix = false;
        bool has_colon_op = false;
        BashAstNode* colon_arg1 = NULL;
        BashAstNode* colon_arg2 = NULL;

        // operator detected after subscript (for ${arr[idx]:-default} etc.)
        String* subscript_op_str = NULL;
        BashAstNode* subscript_op_arg = NULL;

        // scan for subscript and prefix # or !
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
                if (ch_src.length == 1 && ch_src.str[0] == '!' && !has_subscript) {
                    has_bang_prefix = true;
                }
                // detect expansion operators after subscript: :-, :=, :+, :?, -, =, +, ?
                if (has_subscript && !subscript_op_str) {
                    bool is_op = false;
                    if (ch_src.length == 2 && ch_src.str[0] == ':') {
                        char c2 = ch_src.str[1];
                        if (c2 == '-' || c2 == '=' || c2 == '+' || c2 == '?') is_op = true;
                    } else if (ch_src.length == 1) {
                        char c1 = ch_src.str[0];
                        if (c1 == '-' || c1 == '=' || c1 == '+' || c1 == '?') is_op = true;
                    }
                    if (is_op) {
                        subscript_op_str = name_pool_create_len(tp->name_pool, ch_src.str, ch_src.length);
                    }
                }
                // legacy single-colon slice operator
                if (ch_src.length == 1 && ch_src.str[0] == ':' && has_subscript && !subscript_op_str) {
                    has_colon_op = true;
                }
            }
            // collect arguments after colon (for slice)
            if (has_colon_op && is_named && strcmp(ch_type, "subscript") != 0) {
                if (!colon_arg1) colon_arg1 = build_expr_node(tp, ch);
                else if (!colon_arg2) colon_arg2 = build_expr_node(tp, ch);
            }
            // collect argument after expansion operator (for ${arr[idx]:-default})
            if (subscript_op_str && !subscript_op_arg && is_named && strcmp(ch_type, "subscript") != 0) {
                subscript_op_arg = build_expr_node(tp, ch);
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

            if (has_bang_prefix && is_all) {
                // ${!arr[@]} — array keys
                BashArrayKeysNode* keys_node = (BashArrayKeysNode*)alloc_bash_ast_node(
                    tp, BASH_AST_NODE_ARRAY_KEYS, node, sizeof(BashArrayKeysNode));
                keys_node->name = arr_name;
                return (BashAstNode*)keys_node;
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
            // subscript stays as an expression; the transpiler will wrap it with
            // bash_arith_eval_value for indexed (non-associative) arrays
            access->index = build_expr_node(tp, index_node);

            // if no expansion operator, return plain array access
            if (!subscript_op_str) {
                return (BashAstNode*)access;
            }

            // ${arr[idx]:-default} — array access with expansion operator
            BashExpansionNode* sub_exp = (BashExpansionNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_EXPANSION, node, sizeof(BashExpansionNode));
            sub_exp->variable = arr_name;
            sub_exp->inner_expr = (BashAstNode*)access;
            sub_exp->argument = subscript_op_arg;
            // detect operator type
            const char* op_chars = subscript_op_str->chars;
            int op_len = subscript_op_str->len;
            if (op_len == 2 && op_chars[0] == ':') {
                sub_exp->has_colon = true;
                if      (op_chars[1] == '-') sub_exp->expand_type = BASH_EXPAND_DEFAULT;
                else if (op_chars[1] == '=') sub_exp->expand_type = BASH_EXPAND_ASSIGN_DEFAULT;
                else if (op_chars[1] == '+') sub_exp->expand_type = BASH_EXPAND_ALT;
                else if (op_chars[1] == '?') sub_exp->expand_type = BASH_EXPAND_ERROR;
            } else if (op_len == 1) {
                sub_exp->has_colon = false;
                if      (op_chars[0] == '-') sub_exp->expand_type = BASH_EXPAND_DEFAULT;
                else if (op_chars[0] == '=') sub_exp->expand_type = BASH_EXPAND_ASSIGN_DEFAULT;
                else if (op_chars[0] == '+') sub_exp->expand_type = BASH_EXPAND_ALT;
                else if (op_chars[0] == '?') sub_exp->expand_type = BASH_EXPAND_ERROR;
            }
            return (BashAstNode*)sub_exp;
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
                if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '-') {
                    expansion->expand_type = BASH_EXPAND_DEFAULT;
                    expansion->has_colon = true;
                } else if (ch_src.length == 1 && ch_src.str[0] == '-') {
                    expansion->expand_type = BASH_EXPAND_DEFAULT;
                    expansion->has_colon = false;
                } else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '=') {
                    expansion->expand_type = BASH_EXPAND_ASSIGN_DEFAULT;
                    expansion->has_colon = true;
                } else if (ch_src.length == 1 && ch_src.str[0] == '=') {
                    expansion->expand_type = BASH_EXPAND_ASSIGN_DEFAULT;
                    expansion->has_colon = false;
                } else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '+') {
                    expansion->expand_type = BASH_EXPAND_ALT;
                    expansion->has_colon = true;
                } else if (ch_src.length == 1 && ch_src.str[0] == '+') {
                    expansion->expand_type = BASH_EXPAND_ALT;
                    expansion->has_colon = false;
                } else if (ch_src.length == 2 && ch_src.str[0] == ':' && ch_src.str[1] == '?') {
                    expansion->expand_type = BASH_EXPAND_ERROR;
                    expansion->has_colon = true;
                } else if (ch_src.length == 1 && ch_src.str[0] == '?') {
                    expansion->expand_type = BASH_EXPAND_ERROR;
                    expansion->has_colon = false;
                }
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

        // named children: variable_name, special_variable_name, word, regex, number
        if (strcmp(ch_type, "variable_name") == 0 || strcmp(ch_type, "special_variable_name") == 0) {
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
        } else if (strcmp(child_type, "ERROR") == 0) {
            // tree-sitter-bash ERROR recovery: multiline for word list
            // e.g., `for x\nin x\ndo` → ERROR node contains "in" + word list
            uint32_t ec = ts_node_named_child_count(child);
            bool found_in = false;
            for (uint32_t j = 0; j < ec; j++) {
                TSNode err_child = ts_node_named_child(child, j);
                const char* ect = ts_node_type(err_child);
                // Skip the "in" keyword
                if (!found_in && strcmp(ect, "word") == 0) {
                    String* wtext = node_text(tp, err_child);
                    if (wtext && wtext->len == 2 && memcmp(wtext->chars, "in", 2) == 0) {
                        found_in = true;
                        continue;
                    }
                }
                if (found_in) {
                    // Treat remaining children as word list items
                    BashAstNode* word = NULL;
                    if (strcmp(ect, "word") == 0) word = build_word(tp, err_child);
                    else if (strcmp(ect, "string") == 0) word = build_string_node(tp, err_child);
                    else if (strcmp(ect, "raw_string") == 0) word = build_raw_string(tp, err_child);
                    else if (strcmp(ect, "ansi_c_string") == 0) word = build_ansi_c_string(tp, err_child);
                    else if (strcmp(ect, "simple_expansion") == 0 ||
                             strcmp(ect, "expansion") == 0) word = build_expansion(tp, err_child);
                    else if (strcmp(ect, "number") == 0) word = build_word(tp, err_child);
                    if (word) {
                        if (!for_node->words) { for_node->words = word; }
                        else { words_tail->next = word; }
                        words_tail = word;
                    }
                }
            }
        } else if (strcmp(child_type, "do_group") == 0 ||
                   strcmp(child_type, "compound_statement") == 0) {
            for_node->body = build_block(tp, child);
         } else if (strcmp(child_type, "word") == 0 ||
                 strcmp(child_type, "string") == 0 ||
                 strcmp(child_type, "raw_string") == 0 ||
                 strcmp(child_type, "ansi_c_string") == 0 ||
                 strcmp(child_type, "brace_expression") == 0 ||
                   strcmp(child_type, "simple_expansion") == 0 ||
                   strcmp(child_type, "expansion") == 0 ||
                   strcmp(child_type, "number") == 0 ||
                   strcmp(child_type, "concatenation") == 0) {
            BashAstNode* word;
            if (strcmp(child_type, "string") == 0) word = build_string_node(tp, child);
            else if (strcmp(child_type, "raw_string") == 0) word = build_raw_string(tp, child);
             else if (strcmp(child_type, "ansi_c_string") == 0) word = build_ansi_c_string(tp, child);
            else if (strcmp(child_type, "simple_expansion") == 0 ||
                     strcmp(child_type, "expansion") == 0) word = build_expansion(tp, child);
            else if (strcmp(child_type, "concatenation") == 0) word = build_concatenation(tp, child);
            else word = build_word(tp, child);  // word, number, brace_expression

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

        if (strcmp(child_type, "do_group") == 0) {
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

    // check if the case statement source contains extglob operators
    // that tree-sitter may have misparsed
    StrView case_src = bash_node_source(tp, node);
    bool case_has_extglob = false;
    for (size_t si = 0; si + 1 < case_src.length; si++) {
        char c = case_src.str[si];
        if ((c == '@' || c == '?' || c == '+' || c == '!') && case_src.str[si + 1] == '(') {
            case_has_extglob = true;
            break;
        }
    }

    // find the byte offset of the end of "in" keyword in the case statement
    // by scanning the anonymous children
    uint32_t in_end_byte = 0;
    {
        uint32_t all_count = ts_node_child_count(node);
        for (uint32_t k = 0; k < all_count; k++) {
            TSNode anon = ts_node_child(node, k);
            StrView sv = bash_node_source(tp, anon);
            if (sv.length == 2 && memcmp(sv.str, "in", 2) == 0) {
                in_end_byte = ts_node_end_byte(anon);
                break;
            }
        }
    }

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "word") == 0 ||
            strcmp(child_type, "number") == 0 ||
            strcmp(child_type, "simple_expansion") == 0 ||
            strcmp(child_type, "expansion") == 0 ||
            strcmp(child_type, "string") == 0 ||
            strcmp(child_type, "concatenation") == 0 ||
            strcmp(child_type, "command_substitution") == 0 ||
            strcmp(child_type, "ansi_c_string") == 0) {
            if (!case_node->word) {
                case_node->word = build_expr_node(tp, child);
            }
        } else if (strcmp(child_type, "case_item") == 0) {
            BashCaseItemNode* item = (BashCaseItemNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_CASE_ITEM, child, sizeof(BashCaseItemNode));

            uint32_t item_children = ts_node_named_child_count(child);
            BashAstNode* patterns_tail = NULL;
            bool found_patterns = false;

            // check for gap text that tree-sitter dropped before this case_item
            // (e.g., "/dev/@" was consumed before the case_item started)
            const char* gap_prefix = NULL;
            int gap_prefix_len = 0;
            if (case_has_extglob && in_end_byte > 0) {
                uint32_t item_start = ts_node_start_byte(child);
                // look at text between the anchor and the case_item start
                const char* gap = tp->source + in_end_byte;
                size_t gap_len = item_start - in_end_byte;
                // skip leading whitespace
                size_t gstart = 0;
                while (gstart < gap_len && (gap[gstart] == ' ' || gap[gstart] == '\t' ||
                       gap[gstart] == '\n' || gap[gstart] == '\r'))
                    gstart++;
                if (gstart < gap_len) {
                    gap_prefix = gap + gstart;
                    gap_prefix_len = (int)(gap_len - gstart);
                }
            }

            // build patterns and body using raw source when extglob + gap detected
            if (gap_prefix && gap_prefix_len > 0) {
                // extract the full pattern by combining gap_prefix + case_item raw source
                StrView item_src = bash_node_source(tp, child);
                // the pattern area: gap_prefix + item_src up to the closing ) at depth 0
                // allocate a combined buffer
                size_t combined_len = gap_prefix_len + item_src.length;
                char* combined = (char*)alloca(combined_len + 1);
                memcpy(combined, gap_prefix, gap_prefix_len);
                memcpy(combined + gap_prefix_len, item_src.str, item_src.length);
                combined[combined_len] = '\0';

                // find the pattern end (closing ) at depth 0), tracking extglob parens
                int depth = 0;
                size_t pat_start = 0;
                size_t pat_end = combined_len;
                for (size_t si = 0; si < combined_len; si++) {
                    char c = combined[si];
                    if (c == '\\' && si + 1 < combined_len) { si++; continue; }
                    if ((c == '@' || c == '?' || c == '+' || c == '*' || c == '!') &&
                        si + 1 < combined_len && combined[si + 1] == '(') {
                        depth++;
                        si++; // skip '('
                        continue;
                    }
                    if (c == '(' && depth > 0) { depth++; continue; }
                    if (c == ')' && depth > 0) { depth--; continue; }
                    if (depth == 0 && c == ')') {
                        pat_end = si;
                        break;
                    }
                    if (depth == 0 && c == '|') {
                        // pattern separator
                        size_t plen = si - pat_start;
                        if (plen > 0) {
                            BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                                tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                            word->text = name_pool_create_len(tp->name_pool,
                                combined + pat_start, (int)plen);
                            if (!item->patterns) {
                                item->patterns = (BashAstNode*)word;
                            } else {
                                patterns_tail->next = (BashAstNode*)word;
                            }
                            patterns_tail = (BashAstNode*)word;
                        }
                        pat_start = si + 1;
                    }
                }
                // emit last pattern
                if (pat_end > pat_start) {
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool,
                        combined + pat_start, (int)(pat_end - pat_start));
                    if (!item->patterns) {
                        item->patterns = (BashAstNode*)word;
                    } else {
                        patterns_tail->next = (BashAstNode*)word;
                    }
                    patterns_tail = (BashAstNode*)word;
                }
                found_patterns = (item->patterns != NULL);

                // build body from non-pattern children
                for (uint32_t j = 0; j < item_children; j++) {
                    TSNode item_child = ts_node_named_child(child, j);
                    const char* item_child_type = ts_node_type(item_child);
                    if (strcmp(item_child_type, "word") == 0 ||
                        strcmp(item_child_type, "number") == 0 ||
                        strcmp(item_child_type, "extglob_pattern") == 0 ||
                        strcmp(item_child_type, "simple_expansion") == 0 ||
                        strcmp(item_child_type, "expansion") == 0 ||
                        strcmp(item_child_type, "string") == 0 ||
                        strcmp(item_child_type, "concatenation") == 0 ||
                        strcmp(item_child_type, "raw_string") == 0 ||
                        strcmp(item_child_type, "ansi_c_string") == 0) {
                        continue;
                    }
                    BashAstNode* stmt = build_statement(tp, item_child);
                    if (stmt) {
                        if (!item->body) {
                            item->body = stmt;
                        } else {
                            BashAstNode* tail = item->body;
                            while (tail->next) tail = tail->next;
                            tail->next = stmt;
                        }
                    }
                }
            } else {
            // check if the case_item itself contains extglob operators
            StrView item_src = bash_node_source(tp, child);
            bool has_extglob_in_item = false;
            bool has_dollar_in_item = false;
            for (size_t si = 0; si + 1 < item_src.length; si++) {
                char c = item_src.str[si];
                if ((c == '@' || c == '?' || c == '+' || c == '!') && item_src.str[si + 1] == '(') {
                    has_extglob_in_item = true;
                }
                if (c == '$') has_dollar_in_item = true;
            }

            if (has_extglob_in_item && !has_dollar_in_item) {
                // extract patterns from case_item raw source
                const char* src = item_src.str;
                size_t len = item_src.length;
                int depth = 0;
                size_t pat_start = 0;
                size_t pat_end = len;

                for (size_t si = 0; si < len; si++) {
                    char c = src[si];
                    if (c == '\\' && si + 1 < len) { si++; continue; }
                    if ((c == '@' || c == '?' || c == '+' || c == '*' || c == '!') &&
                        si + 1 < len && src[si + 1] == '(') {
                        depth++;
                        si++;
                        continue;
                    }
                    if (c == '(' && depth > 0) { depth++; continue; }
                    if (c == ')' && depth > 0) { depth--; continue; }
                    if (depth == 0 && c == ')') {
                        pat_end = si;
                        break;
                    }
                    if (depth == 0 && c == '|') {
                        size_t plen = si - pat_start;
                        if (plen > 0) {
                            BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                                tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                            word->text = name_pool_create_len(tp->name_pool,
                                src + pat_start, (int)plen);
                            if (!item->patterns) {
                                item->patterns = (BashAstNode*)word;
                            } else {
                                patterns_tail->next = (BashAstNode*)word;
                            }
                            patterns_tail = (BashAstNode*)word;
                        }
                        pat_start = si + 1;
                    }
                }
                if (pat_end > pat_start) {
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool,
                        src + pat_start, (int)(pat_end - pat_start));
                    if (!item->patterns) {
                        item->patterns = (BashAstNode*)word;
                    } else {
                        patterns_tail->next = (BashAstNode*)word;
                    }
                    patterns_tail = (BashAstNode*)word;
                }
                found_patterns = (item->patterns != NULL);

                for (uint32_t j = 0; j < item_children; j++) {
                    TSNode item_child = ts_node_named_child(child, j);
                    const char* item_child_type = ts_node_type(item_child);
                    if (strcmp(item_child_type, "word") == 0 ||
                        strcmp(item_child_type, "number") == 0 ||
                        strcmp(item_child_type, "extglob_pattern") == 0 ||
                        strcmp(item_child_type, "simple_expansion") == 0 ||
                        strcmp(item_child_type, "expansion") == 0 ||
                        strcmp(item_child_type, "string") == 0 ||
                        strcmp(item_child_type, "concatenation") == 0 ||
                        strcmp(item_child_type, "raw_string") == 0 ||
                        strcmp(item_child_type, "ansi_c_string") == 0) {
                        continue;
                    }
                    BashAstNode* stmt = build_statement(tp, item_child);
                    if (stmt) {
                        if (!item->body) {
                            item->body = stmt;
                        } else {
                            BashAstNode* tail = item->body;
                            while (tail->next) tail = tail->next;
                            tail->next = stmt;
                        }
                    }
                }
            } else {
            for (uint32_t j = 0; j < item_children; j++) {
                TSNode item_child = ts_node_named_child(child, j);
                const char* item_child_type = ts_node_type(item_child);

                if (strcmp(item_child_type, "word") == 0 ||
                    strcmp(item_child_type, "number") == 0 ||
                    strcmp(item_child_type, "extglob_pattern") == 0 ||
                    strcmp(item_child_type, "simple_expansion") == 0 ||
                    strcmp(item_child_type, "expansion") == 0 ||
                    strcmp(item_child_type, "string") == 0 ||
                    strcmp(item_child_type, "concatenation") == 0 ||
                    strcmp(item_child_type, "raw_string") == 0 ||
                    strcmp(item_child_type, "ansi_c_string") == 0) {
                    BashAstNode* pattern = build_expr_node(tp, item_child);
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
            } // end else no-extglob in item
            } // end else no-gap

            // update the anchor for gap detection on subsequent items
            if (case_has_extglob) {
                in_end_byte = ts_node_end_byte(child);
            }

            // detect terminator type (;;, ;&, ;;&) from anonymous children
            item->terminator = 0; // default: ;;
            uint32_t all_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < all_child_count; j++) {
                TSNode anon = ts_node_child(child, j);
                StrView sv = bash_node_source(tp, anon);
                if (sv.length == 3 && memcmp(sv.str, ";;&", 3) == 0) {
                    item->terminator = 2;
                    break;
                } else if (sv.length == 2 && memcmp(sv.str, ";&", 2) == 0) {
                    item->terminator = 1;
                    break;
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
    // detect (( ... )) arithmetic compound statement vs { ... } brace group
    TSNode first_anon = ts_node_child(node, 0);
    if (!ts_node_is_null(first_anon)) {
        StrView tok = bash_node_source(tp, first_anon);
        if (tok.length == 2 && tok.str[0] == '(' && tok.str[1] == '(') {
            // (( expr )) — build as arithmetic expression
            BashArithExprNode* arith = (BashArithExprNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ARITHMETIC_EXPR, node, sizeof(BashArithExprNode));
            uint32_t nc = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode child = ts_node_named_child(node, i);
                arith->expression = build_arith_expression(tp, child);
            }
            return (BashAstNode*)arith;
        }
    }
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
        } else if (strcmp(child_type, "ansi_c_string") == 0) {
            assign->value = build_ansi_c_string(tp, child);
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

    // register in scope (but not subscript assignments like arr[idx]=val)
    if (assign->name && !assign->index) {
        bash_scope_define(tp, assign->name, (BashAstNode*)assign, BASH_VAR_GLOBAL);
    }

    return (BashAstNode*)assign;
}

static BashAstNode* build_declaration(BashTranspiler* tp, TSNode node) {
    // local var=value, export var=value, declare [-aAirxlup] var=value
    // first child is the keyword (local, export, declare, typeset)
    StrView source = bash_node_source(tp, node);
    bool is_local = (source.length >= 5 && strncmp(source.str, "local", 5) == 0);
    bool is_export = (source.length >= 6 && strncmp(source.str, "export", 6) == 0);
    bool is_readonly = (source.length >= 8 && strncmp(source.str, "readonly", 8) == 0);
    bool is_declare = (source.length >= 7 && strncmp(source.str, "declare", 7) == 0) ||
                      (source.length >= 7 && strncmp(source.str, "typeset", 7) == 0);
    // typeset/declare inside functions create local variables (like local)
    if (is_declare) is_local = true;

    // parse declare/local flags from source: "declare -Ai var=value" or "local -i var=value"
    int declare_flags = BASH_ATTR_NONE;
    if (is_readonly) declare_flags |= BASH_ATTR_READONLY;
    if (is_declare || is_local) {
        const char* p = source.str;
        const char* end = source.str + source.length;
        // skip keyword
        while (p < end && *p != ' ' && *p != '\t') p++;
        // parse flag groups
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == '-') {
                p++; // skip -
                while (p < end && *p != ' ' && *p != '\t') {
                    switch (*p) {
                        case 'a': declare_flags |= BASH_ATTR_INDEXED_ARRAY; break;
                        case 'A': declare_flags |= BASH_ATTR_ASSOC_ARRAY; break;
                        case 'i': declare_flags |= BASH_ATTR_INTEGER; break;
                        case 'r': declare_flags |= BASH_ATTR_READONLY; break;
                        case 'x': declare_flags |= BASH_ATTR_EXPORT; break;
                        case 'l': declare_flags |= BASH_ATTR_LOWERCASE; break;
                        case 'u': declare_flags |= BASH_ATTR_UPPERCASE; break;
                        case 'p': declare_flags |= BASH_ATTR_PRINT; break;
                        case 'n': declare_flags |= BASH_ATTR_NAMEREF; break;
                        default: break;
                    }
                    p++;
                }
            } else {
                break; // reached variable names/assignments
            }
        }
        if (declare_flags & BASH_ATTR_EXPORT) is_export = true;
    }

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
                assign->declare_flags = declare_flags;
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
            // declaration without assignment: local var, export var, declare -A map
            // skip flag words like "-A", "-i", etc.
            StrView child_src = bash_node_source(tp, child);
            if (child_src.length > 0 && child_src.str[0] == '-') continue;

            BashAssignmentNode* assign = (BashAssignmentNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ASSIGNMENT, child, sizeof(BashAssignmentNode));
            assign->name = node_text(tp, child);
            assign->is_local = is_local;
            assign->is_export = is_export;
            assign->declare_flags = declare_flags;
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
        num->value = strtoll(src.str, NULL, 0); // base 0: auto-detect hex (0x), octal (0), decimal
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
    if (strcmp(node_type, "expansion") == 0) {
        // ${#arr[@]}, ${var}, etc. inside arithmetic context
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
        // first pass: find the operator to determine if this is an assignment
        uint32_t bin_children = ts_node_child_count(node);
        BashOperator op = BASH_OP_ADD;
        for (uint32_t j = 0; j < bin_children; j++) {
            TSNode bc = ts_node_child(node, j);
            if (!ts_node_is_named(bc)) {
                StrView op_src = bash_node_source(tp, bc);
                op = bash_operator_from_string(op_src.str, op_src.length);
                break;
            }
        }
        // if the operator is assignment-like, create an ARITH_ASSIGN node
        if (op == BASH_OP_ASSIGN || op == BASH_OP_ADD_ASSIGN || op == BASH_OP_SUB_ASSIGN ||
            op == BASH_OP_MUL_ASSIGN || op == BASH_OP_DIV_ASSIGN || op == BASH_OP_MOD_ASSIGN) {
            BashArithAssignNode* assign = (BashArithAssignNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_ARITH_ASSIGN, node, sizeof(BashArithAssignNode));
            assign->op = op;
            int operand_idx = 0;
            for (uint32_t j = 0; j < bin_children; j++) {
                TSNode c = ts_node_child(node, j);
                if (ts_node_is_named(c)) {
                    if (operand_idx == 0) {
                        // left side: variable name
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
                        assign->value = build_arith_expression(tp, c);
                    }
                }
            }
            return (BashAstNode*)assign;
        }
        // normal binary arithmetic/comparison
        BashArithBinaryNode* bin = (BashArithBinaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_BINARY, node, sizeof(BashArithBinaryNode));
        bin->op = op;
        int operand_idx = 0;
        for (uint32_t j = 0; j < bin_children; j++) {
            TSNode bc = ts_node_child(node, j);
            if (ts_node_is_named(bc)) {
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
    if (strcmp(node_type, "ternary_expression") == 0) {
        // condition ? then_expr : else_expr
        BashArithTernaryNode* ternary = (BashArithTernaryNode*)alloc_bash_ast_node(
            tp, BASH_AST_NODE_ARITH_TERNARY, node, sizeof(BashArithTernaryNode));
        uint32_t operand_idx = 0;
        uint32_t ch_count = ts_node_named_child_count(node);
        for (uint32_t j = 0; j < ch_count; j++) {
            TSNode c = ts_node_named_child(node, j);
            if (operand_idx == 0) {
                ternary->condition = build_arith_expression(tp, c);
            } else if (operand_idx == 1) {
                ternary->then_expr = build_arith_expression(tp, c);
            } else {
                ternary->else_expr = build_arith_expression(tp, c);
            }
            operand_idx++;
        }
        return (BashAstNode*)ternary;
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
            // for == and != operators, the right side is a glob pattern:
            // tree-sitter may fail to parse patterns like a@(b)c as a single
            // extglob token. recover by extracting raw text from after the
            // operator to end of the binary_expression.
            if ((bin->op == BASH_TEST_STR_EQ || bin->op == BASH_TEST_STR_NE) &&
                !ts_node_is_null(op_node)) {
                uint32_t op_end = ts_node_end_byte(op_node);
                uint32_t expr_end = ts_node_end_byte(node);
                // skip whitespace after operator
                while (op_end < expr_end && (tp->source[op_end] == ' ' || tp->source[op_end] == '\t'))
                    op_end++;
                const char* raw = tp->source + op_end;
                size_t raw_len = expr_end - op_end;
                // trim trailing whitespace
                while (raw_len > 0 && (raw[raw_len - 1] == ' ' || raw[raw_len - 1] == '\t'))
                    raw_len--;
                bool has_extglob_op = false;
                for (size_t i = 0; i + 1 < raw_len; i++) {
                    char c = raw[i];
                    if ((c == '@' || c == '?' || c == '+' || c == '*' || c == '!') &&
                        raw[i + 1] == '(') {
                        has_extglob_op = true;
                        break;
                    }
                }
                if (has_extglob_op && !memchr(raw, '$', raw_len)) {
                    // pure literal + extglob pattern: use raw source as-is
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, bin_right, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool, raw, (int)raw_len);
                    bin->right = (BashAstNode*)word;
                    bin->op = (bin->op == BASH_TEST_STR_EQ) ? BASH_TEST_STR_GLOB : BASH_TEST_STR_NE;
                } else {
                    bin->right = build_test_expression(tp, bin_right);
                }
            } else {
                bin->right = build_test_expression(tp, bin_right);
            }
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

    auto append_elem = [&](BashAstNode* elem) {
        if (!elem) return;
        if (!arr->elements) {
            arr->elements = elem;
        } else {
            tail->next = elem;
        }
        tail = elem;
        arr->length++;
    };

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        BashAstNode* elem;
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "string") == 0) elem = build_string_node(tp, child);
        else if (strcmp(child_type, "raw_string") == 0) elem = build_raw_string(tp, child);
        else if (strcmp(child_type, "ansi_c_string") == 0) elem = build_ansi_c_string(tp, child);
        else {
            // workaround for tree-sitter-bash bug: when a variable assignment
            // follows an array literal, the parser may merge all array words
            // into a single word node (e.g. "alpha beta" instead of "alpha", "beta").
            // detect this by checking for embedded spaces and split manually.
            uint32_t sb = ts_node_start_byte(child);
            uint32_t eb = ts_node_end_byte(child);
            const char* text = tp->source + sb;
            size_t len = eb - sb;

            // check if this word contains unquoted spaces
            bool has_space = false;
            int quote = 0;
            for (size_t j = 0; j < len; j++) {
                char c = text[j];
                if (!quote && (c == '\'' || c == '"')) { quote = c; continue; }
                if (quote && c == quote) { quote = 0; continue; }
                if (c == '\\' && j + 1 < len) { j++; continue; }
                if (!quote && (c == ' ' || c == '\t')) { has_space = true; break; }
            }

            if (has_space && strcmp(child_type, "word") == 0) {
                // split on unquoted whitespace into multiple word nodes
                quote = 0;
                size_t word_start = 0;
                bool in_word = false;
                for (size_t j = 0; j <= len; j++) {
                    char c = (j < len) ? text[j] : ' '; // force flush at end
                    bool is_ws = false;
                    if (!quote && (c == '\'' || c == '"')) { quote = c; in_word = true; continue; }
                    if (quote && c == quote) { quote = 0; in_word = true; continue; }
                    if (c == '\\' && j + 1 < len) { j++; in_word = true; continue; }
                    if (!quote && (c == ' ' || c == '\t')) is_ws = true;

                    if (is_ws) {
                        if (in_word) {
                            size_t wlen = j - word_start;
                            BashWordNode* w = (BashWordNode*)alloc_bash_ast_node(
                                tp, BASH_AST_NODE_WORD, child, sizeof(BashWordNode));
                            w->text = name_pool_create_len(tp->name_pool, text + word_start, wlen);
                            append_elem((BashAstNode*)w);
                            in_word = false;
                        }
                    } else {
                        if (!in_word) {
                            word_start = j;
                            in_word = true;
                        }
                    }
                }
                continue;
            }

            elem = build_word(tp, child);
        }

        append_elem(elem);
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
                                gap->no_backslash_escape = true;
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
                                gap->no_backslash_escape = true;
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
                        tail->no_backslash_escape = true;
                        if (!str->parts) str->parts = (BashAstNode*)tail;
                        else parts_tail->next = (BashAstNode*)tail;
                    }
                    heredoc->body = (BashAstNode*)str;
                }
            }
        }
        heredoc->expand = !quoted;

        // if there are file redirects (e.g., cat > file << EOF), wrap in REDIRECTED
        if (file_redirect_count > 0) {
            // fall through to file redirect handling with heredoc as inner
            inner_cmd = (BashAstNode*)heredoc;
        } else {
            return (BashAstNode*)heredoc;
        }
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
            } else if (strcmp(htype, "raw_string") == 0) {
                heredoc->body = build_raw_string(tp, hchild);
            }
        }
        heredoc->expand = true;

        if (file_redirect_count > 0) {
            inner_cmd = (BashAstNode*)heredoc;
        } else {
            return (BashAstNode*)heredoc;
        }
    }

    // non-cat command with herestring: wrap command with HERESTRING redirect
    if (!is_cat && has_herestring && inner_cmd) {
        // build herestring body expression
        BashAstNode* hs_body = NULL;
        uint32_t hsc = ts_node_named_child_count(herestring_node);
        for (uint32_t i = 0; i < hsc; i++) {
            TSNode hchild = ts_node_named_child(herestring_node, i);
            const char* htype = ts_node_type(hchild);
            if (strcmp(htype, "string") == 0) {
                hs_body = build_string_node(tp, hchild);
            } else if (strcmp(htype, "word") == 0) {
                hs_body = build_word(tp, hchild);
            } else if (strcmp(htype, "concatenation") == 0) {
                hs_body = build_concatenation(tp, hchild);
            } else if (strcmp(htype, "simple_expansion") == 0 ||
                       strcmp(htype, "expansion") == 0) {
                hs_body = build_expansion(tp, hchild);
            } else if (strcmp(htype, "raw_string") == 0) {
                hs_body = build_raw_string(tp, hchild);
            }
        }
        if (hs_body) {
            // create a redirect node with HERESTRING mode
            BashRedirectNode* redir = (BashRedirectNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECT, herestring_node, sizeof(BashRedirectNode));
            redir->fd = 0;
            redir->mode = BASH_REDIR_HERESTRING;
            redir->target = hs_body;

            // also build file_redirects as additional redirects
            BashAstNode* redir_list = (BashAstNode*)redir;
            // (file_redirects will be handled below via the normal path)

            // wrap in a redirected node
            BashRedirectedNode* wrapper = (BashRedirectedNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECTED, node, sizeof(BashRedirectedNode));
            wrapper->inner = inner_cmd;
            wrapper->redirects = redir_list;
            return (BashAstNode*)wrapper;
        }
    }

    // non-cat command with heredoc: wrap command with heredoc as stdin
    if (!is_cat && has_heredoc && inner_cmd) {
        // build heredoc body
        BashAstNode* hd_body = NULL;
        uint32_t hrc = ts_node_named_child_count(heredoc_node);
        bool quoted = false;
        for (uint32_t i = 0; i < hrc; i++) {
            TSNode hchild = ts_node_named_child(heredoc_node, i);
            const char* htype = ts_node_type(hchild);
            if (strcmp(htype, "heredoc_start") == 0) {
                StrView delim_src = bash_node_source(tp, hchild);
                if (delim_src.length > 0 && (delim_src.str[0] == '\'' || delim_src.str[0] == '"')) {
                    quoted = true;
                }
            } else if (strcmp(htype, "heredoc_body") == 0) {
                if (quoted) {
                    StrView body_src = bash_node_source(tp, hchild);
                    BashWordNode* word = (BashWordNode*)alloc_bash_ast_node(
                        tp, BASH_AST_NODE_WORD, hchild, sizeof(BashWordNode));
                    word->text = name_pool_create_len(tp->name_pool, body_src.str, body_src.length);
                    hd_body = (BashAstNode*)word;
                } else {
                    // same expansion-building logic as the cat-heredoc path
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
                                gap->no_backslash_escape = true;
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
                        }
                    }
                    if (pos < body_end) {
                        BashWordNode* tail_word = (BashWordNode*)alloc_bash_ast_node(
                            tp, BASH_AST_NODE_WORD, hchild, sizeof(BashWordNode));
                        tail_word->text = name_pool_create_len(tp->name_pool, tp->source + pos, body_end - pos);
                        tail_word->no_backslash_escape = true;
                        if (!str->parts) str->parts = (BashAstNode*)tail_word;
                        else parts_tail->next = (BashAstNode*)tail_word;
                    }
                    hd_body = (BashAstNode*)str;
                }
            }
        }
        if (hd_body) {
            BashRedirectNode* redir = (BashRedirectNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECT, heredoc_node, sizeof(BashRedirectNode));
            redir->fd = 0;
            redir->mode = BASH_REDIR_HEREDOC;
            redir->target = hd_body;

            BashRedirectedNode* wrapper = (BashRedirectedNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECTED, node, sizeof(BashRedirectedNode));
            wrapper->inner = inner_cmd;
            wrapper->redirects = (BashAstNode*)redir;
            return (BashAstNode*)wrapper;
        }
    }

    // attach file_redirect nodes to the command
    if (file_redirect_count > 0 && inner_cmd) {
        // build the redirect node list (shared logic for COMMAND and non-COMMAND inner)
        BashAstNode* redir_head = NULL;
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

            if (!redir_head) {
                redir_head = (BashAstNode*)redir;
            } else {
                redir_tail->next = (BashAstNode*)redir;
            }
            redir_tail = (BashAstNode*)redir;
        }

        if (inner_cmd->node_type == BASH_AST_NODE_COMMAND) {
            // attach redirects directly to the command node
            BashCommandNode* cmd = (BashCommandNode*)inner_cmd;
            if (!cmd->redirects) {
                cmd->redirects = redir_head;
            } else {
                // append to existing redirect list
                BashAstNode* tail = cmd->redirects;
                while (tail->next) tail = tail->next;
                tail->next = redir_head;
            }
        } else {
            // inner is a pipeline, subshell, compound, list, etc. — wrap it
            BashRedirectedNode* wrapper = (BashRedirectedNode*)alloc_bash_ast_node(
                tp, BASH_AST_NODE_REDIRECTED, node, sizeof(BashRedirectedNode));
            wrapper->inner = inner_cmd;
            wrapper->redirects = redir_head;
            return (BashAstNode*)wrapper;
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
    uint32_t last_end_byte = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(program_node, i);

        // Skip children that overlap with a previous statement's byte range.
        // tree-sitter error recovery can produce overlapping nodes.
        uint32_t child_start = ts_node_start_byte(child);
        if (child_start < last_end_byte) {
            log_debug("bash-ast: skipping overlapping child at byte %d (prev end %d)", child_start, last_end_byte);
            continue;
        }

        BashAstNode* stmt = build_statement(tp, child);
        if (!stmt) continue;

        last_end_byte = ts_node_end_byte(child);

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
    if (strcmp(type, "ansi_c_string") == 0)
        return build_ansi_c_string(tp, expr_node);
    return build_word(tp, expr_node);
}
