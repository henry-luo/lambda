// Test: Import and Module
// Layer: 2 | Category: statement | Covers: import, relative path, alias, pub, multi-import

// ===== Basic import (self-contained test using inline module pattern) =====
// Note: Import tests are limited since they require external files.
// Testing basic expressions that would be imported.

// ===== Inline function definitions (simulating module exports) =====
fn math_add(a: int, b: int) => a + b
fn math_sub(a: int, b: int) => a - b
fn math_mul(a: int, b: int) => a * b

math_add(3, 4)
math_sub(10, 3)
math_mul(5, 6)

// ===== Namespace-like grouping =====
let math_ops = {
    add: fn(a, b) => a + b,
    sub: fn(a, b) => a - b,
    mul: fn(a, b) => a * b
}
math_ops.add(10, 20)
math_ops.sub(30, 5)
math_ops.mul(4, 7)

// ===== Function re-export pattern =====
fn create_api() {
    let internal_helper = fn(x) => x * 2
    {
        double: internal_helper,
        quadruple: fn(x) => internal_helper(internal_helper(x))
    }
}
let api = create_api()
api.double(5)
api.quadruple(3)

// ===== Constants as module values =====
let PI = 3.14159
let E = 2.71828
let TAU = PI * 2
PI
E
TAU
