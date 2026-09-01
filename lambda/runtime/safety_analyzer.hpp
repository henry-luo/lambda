/** @file safety_analyzer.hpp Tail-recursion analysis for lowering. */

#ifndef SAFETY_ANALYZER_HPP
#define SAFETY_ANALYZER_HPP

// Forward declare AstNode to avoid pulling in all AST headers
struct AstNode;
struct AstFuncNode;
struct AstCallNode;

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

#endif // SAFETY_ANALYZER_HPP
