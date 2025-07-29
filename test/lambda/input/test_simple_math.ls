// Simple test for enhanced math parser
print("Testing advanced LaTeX math features...")

// Test Greek letters
let alpha_result = input('test/input/test_math_greek.txt', {'type': 'math', 'flavor': 'latex'})
print("Greek letters result:")
print(alpha_result)

// Test sum with limits
let sum_result = input('test/input/test_math_sum.txt', {'type': 'math', 'flavor': 'latex'})
print("Sum with limits result:")
print(sum_result)
