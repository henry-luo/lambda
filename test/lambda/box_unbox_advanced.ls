// Advanced box/unbox tests for typed function optimization
// Tests various parameter type combinations, mixed calls, and corner cases

//==============================================================================
// SECTION 1: Basic typed parameter combinations
//==============================================================================

// Pure int params - should use unboxed version (_u suffix)
fn add_int(a: int, b: int) { a + b }
add_int(1, 2)                    // 3
add_int(100, -50)                // 50
add_int(0, 0)                    // 0

// Pure float params
fn add_float(a: float, b: float) { a + b }
add_float(1.5, 2.5)              // 4
add_float(-1.0, 1.0)             // 0

// Mixed int and float params
fn mixed_if(a: int, b: float) { a + b }
mixed_if(10, 2.5)                // 12.5

fn mixed_fi(a: float, b: int) { a + b }
mixed_fi(3.5, 2)                 // 5.5

// Bool params
fn and_bool(a: bool, b: bool) { a and b }
and_bool(true, true)             // true
and_bool(true, false)            // false

fn or_bool(a: bool, b: bool) { a or b }
or_bool(false, false)            // false
or_bool(false, true)             // true

//==============================================================================
// SECTION 2: Explicit return types (boxed version already native)
//==============================================================================

// These should NOT generate _u versions (boxed already returns native type)
fn square_explicit(x: int) int { x * x }
square_explicit(5)               // 25
square_explicit(-3)              // 9

fn half_explicit(x: float) float { x / 2.0 }
half_explicit(10.0)              // 5

fn not_explicit(x: bool) bool { not x }
not_explicit(true)               // false

//==============================================================================
// SECTION 3: Mixing boxed and unboxed calls
//==============================================================================

// Call unboxed from unboxed context
fn double_int(x: int) { x * 2 }
fn quad_int(x: int) { double_int(double_int(x)) }
quad_int(5)                      // 20

// Call boxed from unboxed context (type mismatch forces boxed)
fn generic_add(a, b) { a + b }
fn use_generic(x: int) { generic_add(x, 10) }
use_generic(5)                   // 15

// Call with result used in different contexts
fn inc(x: int) { x + 1 }
[inc(1), inc(2), inc(3)]         // [2, 3, 4]
inc(10) + inc(20)                // 32

//==============================================================================
// SECTION 4: Functions with multiple typed params
//==============================================================================

fn three_ints(a: int, b: int, c: int) { a + b + c }
three_ints(1, 2, 3)              // 6

fn four_mixed(a: int, b: float, c: int, d: float) { a + b + c + d }
four_mixed(1, 2.5, 3, 4.5)       // 11

//==============================================================================
// SECTION 5: Nested and chained calls
//==============================================================================

fn mul(a: int, b: int) { a * b }
fn add2(a: int, b: int) { a + b }

// Deeply nested
add2(mul(2, 3), mul(4, 5))       // 6 + 20 = 26

// Chain of calls
add2(add2(add2(1, 2), 3), 4)     // ((1+2)+3)+4 = 10

// Mixed nesting with different types
fn to_int(x: float) { int(x) }
add2(to_int(3.7), to_int(4.2))   // 3 + 4 = 7

//==============================================================================
// SECTION 6: Integer edge cases and overflow
//==============================================================================

// Large values (but within int32 range)
add_int(1000000, 2000000)        // 3000000

// Negative numbers
add_int(-2147483647, 1)          // -2147483646

// Zero operations
mul(0, 12345)                    // 0
mul(12345, 0)                    // 0

//==============================================================================
// SECTION 7: Float precision edge cases
//==============================================================================

fn sub_float(a: float, b: float) { a - b }
sub_float(0.3, 0.1)              // ~0.2 (floating point)

fn div_float(a: float, b: float) { a / b }
div_float(1.0, 3.0)              // ~0.333...

// Very small numbers
add_float(0.0000001, 0.0000002)  // 0.0000003

//==============================================================================
// SECTION 8: Partial typed params (some typed, some untyped)
//==============================================================================

fn partial_typed(a: int, b) { a + b }
partial_typed(10, 20)            // 30
partial_typed(10, 3.5)           // 13.5

fn partial_typed2(a, b: int) { a + b }
partial_typed2(5, 10)            // 15

//==============================================================================
// SECTION 9: Functions returning expressions with mixed types
//==============================================================================

// Result type inferred from expression
fn complex_expr(a: int, b: int) { 
    if (a > b) a - b 
    else b - a 
}
complex_expr(10, 3)              // 7
complex_expr(3, 10)              // 7

// Ternary with different result types - returns Item
fn ternary_mixed(a: int, cond: bool) {
    if (cond) a else "not a number"
}
ternary_mixed(42, true)          // 42
ternary_mixed(42, false)         // "not a number"

//==============================================================================
// SECTION 10: Variables and expressions as arguments
//==============================================================================

let x = 10
let y = 20
add_int(x, y)                    // 30
add_int(x + 1, y - 1)            // 11 + 19 = 30
add_int(x * 2, y - 10)           // 20 + 10 = 30

// Expression results
add_int(3 + 4, 5 + 6)            // 7 + 11 = 18

//==============================================================================
// SECTION 11: First-class function usage (dynamic dispatch)
//==============================================================================

// Assigning function to variable forces dynamic dispatch
let f = fn(a: int, b: int) { a + b }
f(5, 3)                          // 8

// Passing function as argument (must use untyped params for dynamic call)
fn apply(func, a, b) { func(a, b) }
apply(add_int, 10, 20)           // 30

//==============================================================================
// SECTION 12: String type params (no unboxing for strings)
//==============================================================================

// String typed params use boxed version (Item type)
fn get_len(s: string) { len(s) }
get_len("test")                  // 4
get_len("hello world")           // 11

//==============================================================================
// SECTION 13: Forward-referenced function calls (call before definition)
//==============================================================================

// Call function before it's defined - must properly box return value
forward_mul(2, 3.0)              // 6 (float returned, must be boxed as Item)
forward_add_int(5, 7)            // 12 (int returned)
forward_concat("hello", " world") // "hello world" (string returned)
forward_negate(true)             // false (bool returned)

// Forward-declared functions defined later
pub fn forward_mul(a: float, b: float) float => a * b
pub fn forward_add_int(a: int, b: int) int => a + b
fn forward_concat(a: string, b: string) string => a ++ b
fn forward_negate(x: bool) bool => not x

// Mixed: call forward-ref in expression
10 + forward_add_int(3, 4)       // 17

// Forward-ref call inside collection
[forward_add_int(1, 2), forward_add_int(3, 4), forward_add_int(5, 6)]  // [3, 7, 11]

//==============================================================================
// FINAL: Summary result
//==============================================================================

"All box_unbox_advanced tests completed"
