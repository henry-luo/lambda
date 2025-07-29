// Advanced LaTeX Math Parser Tests
fn test_advanced_latex_math() {
    print("Testing advanced LaTeX math features...")
    
    // Test Greek letters
    let alpha_result = input('test/input/test_math_greek.txt', {'type': 'math', 'flavor': 'latex'})
    print("Greek letters test:")
    print(alpha_result)
    
    // Test sum with limits
    let sum_result = input('test/input/test_math_sum.txt', {'type': 'math', 'flavor': 'latex'})
    print("Sum with limits test:")
    print(sum_result)
    
    // Test integral with limits
    let integral_result = input('test/input/test_math_integral.txt', {'type': 'math', 'flavor': 'latex'})
    print("Integral with limits test:")
    print(integral_result)
    
    // Test limit expressions
    let limit_result = input('test/input/test_math_limit.txt', {'type': 'math', 'flavor': 'latex'})
    print("Limit expressions test:")
    print(limit_result)
    
    // Test matrix
    let matrix_result = input('test/input/test_math_matrix.txt', {'type': 'math', 'flavor': 'latex'})
    print("Matrix test:")
    print(matrix_result)
    
    // Test advanced trigonometric functions
    let trig_result = input('test/input/test_math_advanced_trig.txt', {'type': 'math', 'flavor': 'latex'})
    print("Advanced trigonometric functions test:")
    print(trig_result)
}

test_advanced_latex_math()
