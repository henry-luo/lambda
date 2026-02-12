/**
 * @file safety_analyzer.cpp
 * @brief Static safety analysis for stack overflow protection and TCO detection
 *
 * For now, we use a conservative approach: all user-defined functions
 * are considered potentially recursive and get stack checks.
 *
 * Tail Call Optimization: Detects tail-recursive functions that can be
 * transformed into loops to eliminate stack growth.
 */

#include "safety_analyzer.hpp"
#include "ast.hpp"
#include "../lib/log.h"

// Global safety analyzer instance
SafetyAnalyzer* g_safety_analyzer = nullptr;

void init_safety_analyzer() {
    if (!g_safety_analyzer) {
        g_safety_analyzer = new SafetyAnalyzer();
    }
}

void analyze_function_safety(AstNode* module) {
    init_safety_analyzer();
    // For now, we don't do sophisticated analysis
    // All user functions are assumed potentially recursive
    log_debug("Safety analyzer: using conservative approach (all functions get stack checks)");
}

bool function_needs_stack_check(const char* func_name) {
    // Conservative: all user-defined functions need stack checks
    // System functions don't need checks since they're not recursive
    return true;
}

bool function_is_tail_recursive(const char* func_name) {
    // Tail recursion optimization not yet implemented
    return false;
}

// ==============================================================================
// Tail Call Optimization Analysis
// ==============================================================================

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
        // Content expression (procedural) - last statement's return value
        // For now, don't optimize procedural code
        return false;
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
    if (!func_node->name || !func_node->name->chars) {
        log_debug("TCO: skip anonymous function");
        return false;
    }

    // Don't optimize closures (for now) - calling convention complexity
    if (func_node->captures) {
        log_debug("TCO: skip closure '%.*s'",
            (int)func_node->name->len, func_node->name->chars);
        return false;
    }

    // Don't optimize procedures (procedural code has different control flow)
    if (func_node->node_type == AST_NODE_PROC) {
        log_debug("TCO: skip procedure '%.*s'",
            (int)func_node->name->len, func_node->name->chars);
        return false;
    }

    // Check if body has tail call
    if (!func_node->body) return false;

    bool has_tail = has_tail_call(func_node->body, func_node);
    if (has_tail) {
        log_info("TCO: detected tail-recursive function '%.*s'",
            (int)func_node->name->len, func_node->name->chars);
    }
    return has_tail;
}

/**
 * Check if an expression contains ANY recursive call to the given function
 * (regardless of position - tail or non-tail).
 */
static bool has_any_recursive_call(AstNode* expr, AstFuncNode* func_node) {
    if (!expr || !func_node) return false;

    switch (expr->node_type) {
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)expr;
        // Check if this call is recursive
        if (is_recursive_call(call, func_node)) return true;
        // Also check the arguments for recursive calls
        AstNode* arg = call->argument;
        while (arg) {
            if (has_any_recursive_call(arg, func_node)) return true;
            arg = arg->next;
        }
        // Check the callee expression (for chained calls)
        return has_any_recursive_call(call->function, func_node);
    }

    case AST_NODE_PRIMARY:
        return has_any_recursive_call(((AstPrimaryNode*)expr)->expr, func_node);

    case AST_NODE_IF_EXPR:
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)expr;
        if (has_any_recursive_call(if_node->cond, func_node)) return true;
        if (has_any_recursive_call(if_node->then, func_node)) return true;
        if (has_any_recursive_call(if_node->otherwise, func_node)) return true;
        return false;
    }

    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)expr;
        if (has_any_recursive_call(match->scrutinee, func_node)) return true;
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            if (has_any_recursive_call(arm->pattern, func_node)) return true;
            if (has_any_recursive_call(arm->body, func_node)) return true;
            arm = (AstMatchArm*)arm->next;
        }
        return false;
    }

    case AST_NODE_BINARY: {
        AstBinaryNode* bin = (AstBinaryNode*)expr;
        return has_any_recursive_call(bin->left, func_node) ||
               has_any_recursive_call(bin->right, func_node);
    }

    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        return has_any_recursive_call(((AstUnaryNode*)expr)->operand, func_node);

    case AST_NODE_LIST:
    case AST_NODE_CONTENT: {
        AstListNode* list = (AstListNode*)expr;
        AstNode* item = list->item;
        while (item) {
            if (has_any_recursive_call(item, func_node)) return true;
            item = item->next;
        }
        // Also check declarations
        AstNode* decl = list->declare;
        while (decl) {
            if (has_any_recursive_call(decl, func_node)) return true;
            decl = decl->next;
        }
        return false;
    }

    case AST_NODE_ASSIGN: {
        AstNamedNode* asn = (AstNamedNode*)expr;
        return has_any_recursive_call(asn->as, func_node);
    }

    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM: {
        AstForNode* for_node = (AstForNode*)expr;
        if (has_any_recursive_call(for_node->loop, func_node)) return true;
        return has_any_recursive_call(for_node->then, func_node);
    }

    case AST_NODE_LOOP: {
        AstNamedNode* loop = (AstNamedNode*)expr;
        return has_any_recursive_call(loop->as, func_node);
    }

    case AST_NODE_INDEX_EXPR:
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)expr;
        return has_any_recursive_call(field->object, func_node) ||
               has_any_recursive_call(field->field, func_node);
    }

    case AST_NODE_ARRAY: {
        AstArrayNode* arr = (AstArrayNode*)expr;
        AstNode* item = arr->item;
        while (item) {
            if (has_any_recursive_call(item, func_node)) return true;
            item = item->next;
        }
        return false;
    }

    case AST_NODE_MAP:
    case AST_NODE_ELEMENT: {
        AstMapNode* map = (AstMapNode*)expr;
        AstNode* item = map->item;
        while (item) {
            if (has_any_recursive_call(item, func_node)) return true;
            item = item->next;
        }
        if (expr->node_type == AST_NODE_ELEMENT) {
            AstNode* content = ((AstElementNode*)expr)->content;
            while (content) {
                if (has_any_recursive_call(content, func_node)) return true;
                content = content->next;
            }
        }
        return false;
    }

    default:
        // Literals, identifiers, etc. don't contain calls
        return false;
    }
}

/**
 * Check if an expression has recursive calls that are NOT in tail position.
 * This traverses the AST and finds recursive calls, excluding those in tail position.
 */
static bool has_non_tail_recursive_call(AstNode* expr, AstFuncNode* func_node, bool in_tail_position) {
    if (!expr || !func_node) return false;

    switch (expr->node_type) {
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)expr;
        // If this call is recursive and NOT in tail position, it's a non-tail recursive call
        if (is_recursive_call(call, func_node) && !in_tail_position) {
            return true;
        }
        // Check arguments - they are NEVER in tail position
        AstNode* arg = call->argument;
        while (arg) {
            if (has_non_tail_recursive_call(arg, func_node, false)) return true;
            arg = arg->next;
        }
        // Callee is not in tail position either
        return has_non_tail_recursive_call(call->function, func_node, false);
    }

    case AST_NODE_PRIMARY:
        return has_non_tail_recursive_call(((AstPrimaryNode*)expr)->expr, func_node, in_tail_position);

    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)expr;
        // Condition is NOT in tail position
        if (has_non_tail_recursive_call(if_node->cond, func_node, false)) return true;
        // Both branches inherit tail position from parent
        if (has_non_tail_recursive_call(if_node->then, func_node, in_tail_position)) return true;
        if (has_non_tail_recursive_call(if_node->otherwise, func_node, in_tail_position)) return true;
        return false;
    }

    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)expr;
        // scrutinee is not in tail position
        if (has_non_tail_recursive_call(match->scrutinee, func_node, false)) return true;
        // each arm body inherits tail position from parent
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            if (has_non_tail_recursive_call(arm->pattern, func_node, false)) return true;
            if (has_non_tail_recursive_call(arm->body, func_node, in_tail_position)) return true;
            arm = (AstMatchArm*)arm->next;
        }
        return false;
    }

    case AST_NODE_BINARY: {
        // Binary operations: neither operand is in tail position (result is computed after)
        AstBinaryNode* bin = (AstBinaryNode*)expr;
        return has_non_tail_recursive_call(bin->left, func_node, false) ||
               has_non_tail_recursive_call(bin->right, func_node, false);
    }

    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        // Unary operand is not in tail position
        return has_non_tail_recursive_call(((AstUnaryNode*)expr)->operand, func_node, false);

    case AST_NODE_LIST: {
        AstListNode* list = (AstListNode*)expr;
        // Declarations are not in tail position
        AstNode* decl = list->declare;
        while (decl) {
            if (has_non_tail_recursive_call(decl, func_node, false)) return true;
            decl = decl->next;
        }
        // All items except the last are not in tail position
        AstNode* item = list->item;
        while (item) {
            bool is_last = (item->next == nullptr);
            if (has_non_tail_recursive_call(item, func_node, is_last && in_tail_position)) return true;
            item = item->next;
        }
        return false;
    }

    case AST_NODE_ASSIGN: {
        // The assigned expression is not in tail position
        AstNamedNode* asn = (AstNamedNode*)expr;
        return has_non_tail_recursive_call(asn->as, func_node, false);
    }

    default:
        // For other node types, recursively check but assume not in tail position
        // This is conservative but safe
        return has_any_recursive_call(expr, func_node) && !in_tail_position;
    }
}

/**
 * Determine if a function with TCO is fully safe (no stack growth).
 * A TCO function is safe if ALL its recursive calls are in tail position.
 * If there are non-tail recursive calls, it still needs stack checks.
 */
bool is_tco_function_safe(AstFuncNode* func_node) {
    if (!func_node || !func_node->body) return false;

    // Check if there are any recursive calls NOT in tail position
    bool has_non_tail = has_non_tail_recursive_call(func_node->body, func_node, true);

    if (has_non_tail) {
        log_debug("TCO: function '%.*s' has non-tail recursive calls, needs stack check",
            (int)func_node->name->len, func_node->name->chars);
        return false;
    }

    log_debug("TCO: function '%.*s' is fully safe (all recursion is tail recursion)",
        (int)func_node->name->len, func_node->name->chars);
    return true;
}

void SafetyAnalyzer::init_system_functions() {
    // In conservative mode, we don't need to track system functions
    // The callback_sys_funcs_ static array is defined below
    log_debug("Safety analyzer: using conservative approach (all functions get stack checks)");
}

// System functions that accept user callbacks - for future use
const char* SafetyAnalyzer::callback_sys_funcs_[] = {
    "map", "filter", "reduce", "fold", "foldl", "foldr",
    "find", "find_index", "any", "all", "none",
    "sort_by", "group_by", "partition",
    "foreach", "transform", "scan",
    "take_while", "drop_while",
    nullptr
};

void SafetyAnalyzer::analyze_module(AstNode* module) {
    // Simplified: no analysis needed with conservative approach
    log_debug("Safety analysis complete (conservative mode)");
}

FunctionSafety SafetyAnalyzer::get_safety(const char* name) const {
    // Conservative: all user functions are unsafe (may recurse)
    (void)name;  // unused in conservative mode
    return FunctionSafety::UNSAFE;
}

bool SafetyAnalyzer::is_safe(const char* name) const {
    (void)name;  // unused in conservative mode
    return false;  // Conservative: no function is provably safe
}

bool SafetyAnalyzer::is_tail_recursive(const char* name) const {
    (void)name;  // unused in conservative mode
    return false;  // TCO not yet implemented
}

void SafetyAnalyzer::dump() const {
    log_info("=== Function Safety Analysis (Conservative Mode) ===");
    log_info("All user-defined functions receive stack overflow checks");
}
