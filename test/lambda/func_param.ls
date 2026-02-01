// Test function parameter handling

// Test 1: Missing arguments - should fill with null
fn two_params(a, b) { [a, b] }
two_params(1)           // Expected: [1, null]
two_params()            // Expected: [null, null]

// Test 2: Extra arguments - should discard with warning
fn one_param(a) { a }
one_param(1, 2, 3)      // Expected: 1 (with warning in log)

// Test 3: Type matching - compatible types (int → float coercion)
fn takes_float(x: float) float { x * 2.0 }
takes_float(5)          // Expected: 10.0
takes_float(5.0)        // Expected: 10.0

// Test 4: Type matching - int parameter
fn takes_int(x: int) int { x + 1 }
takes_int(100)          // Expected: 101

// Test 5: Return type matching - explicit return types
fn returns_int() int { 42 }
returns_int()           // Expected: 42

fn returns_float() float { 3.14 }
returns_float()         // Expected: 3.14

// Test 6: Mixed param and return types
fn process(a: int, b: float) float { a + b }
process(10, 2.5)        // Expected: 12.5

// Test 7: Variadic-like behavior with null filling (optional params pattern)
fn optional_params(required, opt1, opt2) {
    if (opt1 == null) "no opt1"
    else if (opt2 == null) "no opt2"
    else "all present"
}
[optional_params(1), optional_params(1, 2), optional_params(1, 2, 3)]

// Test 8: Functions with no params
fn no_params() { "hello" }
no_params()             // Expected: "hello"
