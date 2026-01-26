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

void SafetyAnalyzer::init_system_functions() {
    // System functions that accept user callbacks - callers should check
    const char* callback_funcs[] = {
        "map", "filter", "reduce", "fold", "foldl", "foldr",
        "find", "find_index", "any", "all", "none",
        "sort_by", "group_by", "partition",
        "foreach", "transform", "scan",
        "take_while", "drop_while",
        nullptr
    };
    
    for (int i = 0; callback_funcs[i]; i++) {
        callback_sys_funcs_.insert(callback_funcs[i]);
    }
}

void SafetyAnalyzer::analyze_module(AstNode* module) {
    // Simplified: no analysis needed with conservative approach
    log_debug("Safety analysis complete (conservative mode)");
}

FunctionSafety SafetyAnalyzer::get_safety(const std::string& name) const {
    // Conservative: all user functions are unsafe (may recurse)
    return FunctionSafety::UNSAFE;
}

bool SafetyAnalyzer::is_safe(const std::string& name) const {
    return false;  // Conservative: no function is provably safe
}

bool SafetyAnalyzer::is_tail_recursive(const std::string& name) const {
    return false;  // TCO not yet implemented
}

void SafetyAnalyzer::dump() const {
    log_info("=== Function Safety Analysis (Conservative Mode) ===");
    log_info("All user-defined functions receive stack overflow checks");
}
