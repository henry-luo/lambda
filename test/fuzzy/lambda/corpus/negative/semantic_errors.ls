// Semantic errors - valid syntax but invalid semantics
// The runtime should handle these gracefully

// These will cause runtime errors but shouldn't crash

// Division by zero (should return inf or error)
// let div_zero = 5 / 0

// Out of bounds access
// let arr = [1, 2, 3]
// let oob = arr[100]

// Invalid type operations
// let type_err = "hello" * true

// Undefined variable
// let undef = nonexistent_var + 5

// Invalid function calls
// fn f(x, y) => x + y
// let missing_arg = f(1)

// Null operations
// let null_op = null + 5

// Note: These are commented to avoid fuzzer errors
// The mutations will generate these patterns dynamically
let valid_placeholder = 1
