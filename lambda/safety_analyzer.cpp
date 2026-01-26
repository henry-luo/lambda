/**
 * @file safety_analyzer.cpp
 * @brief Simplified static safety analysis for stack overflow protection
 * 
 * For now, we use a conservative approach: all user-defined functions
 * are considered potentially recursive and get stack checks.
 * 
 * Future enhancement: implement call graph analysis to identify truly
 * safe (non-recursive) functions that don't need stack checks.
 */

#include "safety_analyzer.hpp"
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
