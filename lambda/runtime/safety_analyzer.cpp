#include "safety_analyzer.hpp"
#include "ast.hpp"
#include "lib/log.h"

// ==============================================================================
// Tail Call Optimization Analysis
// ==============================================================================

// P2 lowering copies the function node, but direct identifiers retain their
// original declaration entry. D8.3.4 requires this private image to retain
// source-definition semantics, including self-tail-call recognition.
static bool same_function_definition(const AstFuncNode* left,
        const AstFuncNode* right) {
    if (left == right) return true;
    if (!left || !right || !left->analysis || !right->analysis) return false;
    NameEntry* left_decl = left->analysis->decl_entry;
    return left_decl && left_decl == right->analysis->decl_entry;
}

/**
 * Check if a call expression is a direct recursive call to the given function.
 * Returns true if call_node calls func_node directly (not through a variable).
 */
bool is_recursive_call(AstCallNode* call_node, AstFuncNode* func_node) {
    if (!call_node || !func_node || !func_node->name) return false;

    AstNode* callee = call_node->function;
    if (!callee) return false;

    // Unwrap primary expression
    if (callee->node_type == AST_NODE_PRIMARY) {
        callee = ((AstPrimaryNode*)callee)->expr;
    }

    // Check if callee is an identifier matching the function name
    if (callee->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)callee;
        if (!ident->name || !ident->entry) return false;

        // Check if the identifier resolves to our function
        AstNode* resolved = ident->entry->node;
        if (resolved == (AstNode*)func_node) {
            return true;
        }

        if ((func_node->analysis &&
                    ident->entry == func_node->analysis->decl_entry) ||
                (resolved && (resolved->node_type == AST_NODE_FUNC ||
                    resolved->node_type == AST_NODE_PROC ||
                    resolved->node_type == AST_NODE_FUNC_EXPR ||
                    resolved->node_type == AST_NODE_ARROW_FUNC) &&
                    same_function_definition((AstFuncNode*)resolved, func_node))) {
            return true;
        }

        // Also check by name match for self-references
        if (ident->name->len == func_node->name->len &&
            memcmp(ident->name->chars, func_node->name->chars, ident->name->len) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * Check if an expression contains a tail call to the given function.
 * A tail call is a recursive call that is the last operation before returning.
 *
 * Tail positions:
 * - Direct call expression: f(x)
 * - Ternary branches: cond ? f(x) : f(y) - both branches are tail positions
 * - Let expression body: (let x = 1, f(x)) - last expression is tail position
 * - Primary wrapping: (f(x)) - unwrap and check inner expression
 */
bool has_tail_call(AstNode* expr, AstFuncNode* func_node) {
    if (!expr || !func_node) return false;

    switch (expr->node_type) {
    case AST_NODE_CALL_EXPR:
        // Direct call - check if it's recursive
        return is_recursive_call((AstCallNode*)expr, func_node);

    case AST_NODE_PRIMARY: {
        // Unwrap primary and check inner expression
        AstPrimaryNode* primary = (AstPrimaryNode*)expr;
        return has_tail_call(primary->expr, func_node);
    }

    case AST_NODE_IF_EXPR: {
        // Both branches must be checked for tail position
        AstIfNode* if_node = (AstIfNode*)expr;
        // If either branch has a tail call, the function is tail-recursive
        // (the condition cannot contain a tail call)
        bool then_tail = if_node->then ? has_tail_call(if_node->then, func_node) : false;
        bool else_tail = if_node->otherwise ? has_tail_call(if_node->otherwise, func_node) : false;
        return then_tail || else_tail;
    }

    case AST_NODE_MATCH_EXPR: {
        // each arm body is in tail position, scrutinee is not
        AstMatchNode* match = (AstMatchNode*)expr;
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            if (has_tail_call(arm->body, func_node)) return true;
            arm = (AstMatchArm*)arm->next;
        }
        return false;
    }

    case AST_NODE_LET_STAM: {
        // The body of a let expression is in tail position
        // Let nodes chain declarations, the expression value is the last node's type
        // For let expressions, we need to find the final expression
        // This is more complex - for now, don't optimize let bodies
        return false;
    }

    case AST_NODE_LIST: {
        // List expression - last item is in tail position
        AstListNode* list = (AstListNode*)expr;
        AstNode* item = list->item;
        if (!item) return false;
        // Find last item
        while (item->next) item = item->next;
        return has_tail_call(item, func_node);
    }

    case AST_NODE_CONTENT: {
        // Direct-parser content can put declarations and side-effect
        // statements before a final value expression. Only that final value
        // is in tail position; otherwise recursive calls retain their frame.
        AstListNode* list = (AstListNode*)expr;
        AstNode* item = list->item;
        if (!item) return false;
        while (item->next) item = item->next;
        if (is_declaration_node(item) ||
                is_side_effect_stam(item->node_type)) {
            return item->node_type == AST_NODE_RETURN_STAM &&
                has_tail_call(((AstReturnNode*)item)->value, func_node);
        }
        return has_tail_call(item, func_node);
    }

    case AST_NODE_BLOCK: {
        AstNode* statement = ((AstBlockNode*)expr)->statements;
        if (!statement) return false;
        while (statement->next) statement = statement->next;
        return has_tail_call(statement, func_node);
    }

    case AST_NODE_EXPR_STMT:
        return has_tail_call(((AstExprStmtNode*)expr)->expression, func_node);

    case AST_NODE_RETURN_STAM: {
        // Return statement - the return value is in tail position
        AstReturnNode* ret = (AstReturnNode*)expr;
        return ret->value ? has_tail_call(ret->value, func_node) : false;
    }

    default:
        // Binary ops, unary ops, literals, etc. are not tail calls
        return false;
    }
}

/**
 * Check if a function should use tail call optimization.
 * A function should use TCO if:
 * 1. It has a name (anonymous functions can't reference themselves)
 * 2. Its body contains a tail-recursive call
 * 3. It's not a closure (closures have complex calling conventions)
 */
bool should_use_tco(AstFuncNode* func_node) {
    if (!func_node) return false;

    // Must have a name (for self-reference)
    if (!func_node->name) {
        log_debug("TCO: skip anonymous function");
        return false;
    }

    // Don't optimize closures (for now) - calling convention complexity
    if (func_node->captures) {
        log_debug("TCO: skip closure '%.*s'",
            (int)func_node->name->len, func_node->name->chars);
        return false;
    }

    // Procedures are now supported for TCO — return statements with tail calls
    // are converted to goto jumps

    // Check if body has tail call
    if (!func_node->body) return false;

    bool has_tail = has_tail_call(func_node->body, func_node);
    if (has_tail) {
        log_info("TCO: detected tail-recursive function '%.*s'",
            (int)func_node->name->len, func_node->name->chars);
    }
    return has_tail;
}
