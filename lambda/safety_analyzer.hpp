/**
 * @file safety_analyzer.hpp
 * @brief Static analysis to classify functions as safe or unsafe for stack overflow
 *        and detect tail-recursive functions for optimization
 * 
 * Safe functions: Cannot cause unbounded recursion
 * Unsafe functions: May cause unbounded recursion, need stack checks
 * Tail-recursive: Direct recursive call in tail position - can use TCO
 * 
 * Current implementation: Conservative approach - all user functions get checks
 */

#ifndef SAFETY_ANALYZER_HPP
#define SAFETY_ANALYZER_HPP

#include <stddef.h>
#include <stdbool.h>

// Forward declare AstNode to avoid pulling in all AST headers
struct AstNode;
struct AstFuncNode;
struct AstCallNode;
struct String;

enum class FunctionSafety {
    UNKNOWN,    // Not yet analyzed
    ANALYZING,  // Currently being analyzed (cycle detection)
    SAFE,       // Proven safe - no stack check needed
    UNSAFE      // May recurse - stack check required
};

// Note: FunctionCallInfo is not currently used in conservative mode.
// When full analysis is implemented, this can use ArrayList and char* instead.
struct FunctionCallInfo {
    const char* name;
    FunctionSafety safety;
    bool is_tail_recursive;
    AstNode* node;
};

/**
 * Tail Call Optimization (TCO) Analysis
 * 
 * Detects if a function is tail-recursive and can be optimized.
 * A function is tail-recursive if:
 * 1. It calls itself recursively
 * 2. The recursive call is in tail position (last operation before return)
 * 
 * Tail positions include:
 * - Direct function body (fn x => f(x))
 * - Both branches of if-expression in tail position
 * - Last expression in let-expression in tail position
 */

/**
 * Check if an expression contains a tail call to the given function.
 * @param expr Expression to analyze
 * @param func_node Function to check for recursive calls to
 * @return true if expr contains a tail call to func_node
 */
bool has_tail_call(AstNode* expr, AstFuncNode* func_node);

/**
 * Check if a call expression is a direct recursive call to the given function.
 * @param call_node Call expression to check
 * @param func_node Function to check for recursion to
 * @return true if call_node is a direct call to func_node
 */
bool is_recursive_call(AstCallNode* call_node, AstFuncNode* func_node);

/**
 * Check if a function is tail-recursive and should use TCO.
 * @param func_node Function to analyze
 * @return true if function should use tail call optimization
 */
bool should_use_tco(AstFuncNode* func_node);

/**
 * Check if a TCO function is fully safe (no stack growth at all).
 * A function is safe if ALL recursive calls are in tail position.
 * This means: if tail calls are removed (transformed to goto), and
 * there are no other recursive calls, then the function is safe.
 * 
 * @param func_node Function to analyze (should already be TCO-eligible)
 * @return true if function is safe after TCO transformation
 */
bool is_tco_function_safe(AstFuncNode* func_node);

class SafetyAnalyzer {
public:
    SafetyAnalyzer() { init_system_functions(); }
    
    /**
     * Analyze all functions in a module and classify their safety.
     * @param module Root AST node of the module
     */
    void analyze_module(AstNode* module);
    
    /**
     * Get the safety classification of a function.
     * @param name Function name
     * @return Safety classification
     */
    FunctionSafety get_safety(const char* name) const;
    
    /**
     * Check if a function is safe (no stack check needed).
     * @param name Function name
     * @return true if safe, false if unsafe or unknown
     */
    bool is_safe(const char* name) const;
    
    /**
     * Check if a function is tail-recursive and can be optimized.
     * @param name Function name
     * @return true if tail-recursive
     */
    bool is_tail_recursive(const char* name) const;
    
    /**
     * Dump analysis results for debugging.
     */
    void dump() const;

private:
    // Note: In conservative mode, these are not used.
    // When full analysis is implemented, use HashMap from lib/hashmap.h
    // For now, we just track callback system functions as a simple array.
    static const char* callback_sys_funcs_[];
    
    void init_system_functions();
};

// Global safety analyzer instance
extern SafetyAnalyzer* g_safety_analyzer;

/**
 * Initialize the global safety analyzer.
 */
void init_safety_analyzer();

/**
 * Analyze a module for function safety.
 */
void analyze_function_safety(AstNode* module);

/**
 * Check if a function requires stack overflow check.
 * @param func_name Function name
 * @return true if stack check is required
 */
bool function_needs_stack_check(const char* func_name);

/**
 * Check if a function can use tail call optimization.
 * @param func_name Function name
 * @return true if TCO can be applied
 */
bool function_is_tail_recursive(const char* func_name);

#endif // SAFETY_ANALYZER_HPP
