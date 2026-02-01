// Negative tests for function parameter handling
// These trigger type errors but transpilation should continue until threshold

// Test 1: Type error - string passed to int param
fn strict_int(x: int) int { x }
strict_int("hello")   // Error: string incompatible with int

// Test 2: Multiple type errors in sequence (tests error accumulation)
fn typed_fn(a: int, b: float, c: string) { [a, b, c] }
typed_fn("wrong", "also wrong", 123)  // 3 type errors, should all be reported

// Test 3: Return type mismatch (warning)
fn bad_return() int { "string" }  // Warning: body returns string, declared int
bad_return()

// Test 4: Mixed valid and invalid calls (error recovery)
fn add_ints(a: int, b: int) int { a + b }
add_ints(1, 2)           // Valid - should work
add_ints("x", "y")       // Error - both args wrong type
add_ints(3, 4)           // Valid - should still work after error

// Test 5: Multiple function calls with various errors
fn float_fn(x: float) { x }
fn string_fn(s: string) { s }
fn bool_fn(b: bool) { b }

float_fn("not a float")   // Error
string_fn(123)            // Error  
bool_fn("not a bool")     // Error

// After errors, valid calls should still work
float_fn(1.5)             // Valid
string_fn("hello")        // Valid
