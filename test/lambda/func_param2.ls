// Comprehensive test for Phase 2 function parameter features
// Tests: optional params with ?, default values, named arguments

// Test 1: Default parameter values
fn add(a, b = 10) => a + b

let r1 = add(5)       // 15
let r2 = add(5, 3)    // 8

// Test 2: Optional parameter with ? marker
fn test_opt(a, b?) => if (b) a + b else a

let r3 = test_opt(5)       // 5
let r4 = test_opt(5, 3)    // 8

// Test 3: Multiple default parameters
fn point3d(x = 0, y = 0, z = 0) => [x, y, z]

let p1 = point3d()           // [0, 0, 0]
let p2 = point3d(1)          // [1, 0, 0]
let p3 = point3d(1, 2)       // [1, 2, 0]
let p4 = point3d(1, 2, 3)    // [1, 2, 3]

// Test 4: Named arguments (positional)
fn rect(left, top, width, height) => [left, top, width, height]
let rect1 = rect(0, 0, 100, 50)   // [0, 0, 100, 50]

// Test 5: Named arguments (all named)
let rect2 = rect(left: 10, top: 20, width: 100, height: 50)  // [10, 20, 100, 50]

// Test 6: Named arguments (reordered)
let rect3 = rect(height: 50, width: 100, top: 20, left: 10)  // [10, 20, 100, 50]

// Test 7: Mix of positional and named
let rect4 = rect(0, 0, width: 200, height: 100)  // [0, 0, 200, 100]

// Test 8: Named arguments with defaults
fn config(host = "localhost", port = 8080, debug = false) => [host, port, debug]

let cfg1 = config()                      // ["localhost", 8080, false]
let cfg2 = config(port: 3000)            // ["localhost", 3000, false]
let cfg3 = config(debug: true)           // ["localhost", 8080, true]
let cfg4 = config(host: "example.com", debug: true)  // ["example.com", 8080, true]

// Test 9: Default value with expression
let multiplier = 10
fn scaled(x, factor = multiplier * 2) => x * factor

let s1 = scaled(5)      // 5 * 20 = 100
let s2 = scaled(5, 3)   // 5 * 3 = 15

// Test 10: Typed parameter with default
fn multiply(x: int, y: int = 2) => x * y

let m1 = multiply(5)      // 10
let m2 = multiply(5, 3)   // 15

// Test 11: Optional typed parameter
// With typed optional params, the default is the zero value (0 for int)
// is_truthy(0) = false for numeric types, so the else branch is taken
fn opt_typed(a: int, b?: int) => if (b) a + b else a * 2

let o1 = opt_typed(5)      // 10 (b=0 which is falsy, so a*2=10)
let o2 = opt_typed(5, 3)   // 8 (5 + 3)

// Output all results
{
    "test1_default": [r1, r2],
    "test2_optional": [r3, r4],
    "test3_multi_default": [p1, p2, p3, p4],
    "test4_positional": rect1,
    "test5_all_named": rect2,
    "test6_reordered": rect3,
    "test7_mixed": rect4,
    "test8_named_with_defaults": [cfg1, cfg2, cfg3, cfg4],
    "test9_expr_default": [s1, s2],
    "test10_typed_default": [m1, m2],
    "test11_optional_typed": [o1, o2]
}
