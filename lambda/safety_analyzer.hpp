/**
 * @file safety_analyzer.hpp
 * @brief Static analysis to classify functions as safe or unsafe for stack overflow
 * 
 * Safe functions: Cannot cause unbounded recursion
 * Unsafe functions: May cause unbounded recursion, need stack checks
 * 
 * Current implementation: Conservative approach - all user functions get checks
 */

#ifndef SAFETY_ANALYZER_HPP
#define SAFETY_ANALYZER_HPP

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

// Forward declare AstNode to avoid pulling in all AST headers
struct AstNode;

enum class FunctionSafety {
    UNKNOWN,    // Not yet analyzed
    ANALYZING,  // Currently being analyzed (cycle detection)
    SAFE,       // Proven safe - no stack check needed
    UNSAFE      // May recurse - stack check required
};

struct FunctionCallInfo {
    std::string name;
    std::vector<std::string> callees;  // Functions this function calls
    std::vector<std::string> callback_args;  // Functions passed as arguments to HOFs
    FunctionSafety safety = FunctionSafety::UNKNOWN;
    bool is_tail_recursive = false;  // Can be optimized with TCO
    AstNode* node = nullptr;
};

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
    FunctionSafety get_safety(const std::string& name) const;
    
    /**
     * Check if a function is safe (no stack check needed).
     * @param name Function name
     * @return true if safe, false if unsafe or unknown
     */
    bool is_safe(const std::string& name) const;
    
    /**
     * Check if a function is tail-recursive and can be optimized.
     * @param name Function name
     * @return true if tail-recursive
     */
    bool is_tail_recursive(const std::string& name) const;
    
    /**
     * Dump analysis results for debugging.
     */
    void dump() const;

private:
    std::unordered_map<std::string, FunctionCallInfo> functions_;
    std::unordered_set<std::string> safe_sys_funcs_;
    std::unordered_set<std::string> callback_sys_funcs_;
    
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
