// Test error union type T^ in parameters
// T^ is shorthand for T | error - accepts both T and error values

// ============================================
// 1. Basic error union parameter
// ============================================

fn process_int(val: int^) int => val + 1

// Passing int to int^ parameter works
process_int(42)

// ============================================
// 2. Multiple parameters with error union
// ============================================

fn add_values(a: int^, b: int^) int => a + b

add_values(10, 20)

// ============================================
// 3. Error union with different types
// ============================================

fn get_length(s: string^) int => len(s)

get_length("hello")

// ============================================
// 4. Explicit union T | error works same as T^
// ============================================

fn explicit_union(val: int | error) int => val * 2

explicit_union(5)
