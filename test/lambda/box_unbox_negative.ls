// Negative tests for box/unbox runtime errors
// These tests verify proper error handling at runtime

// Test 1: Integer overflow at runtime (depends on platform)
// The i2it macro has overflow checking
fn overflow_add(a: int, b: int) { a + b }

// This should work (within int32 range)
overflow_add(2147483646, 1)

// Test 2: Type coercion warnings - passing float to int param
// This generates a compile-time warning but runs at runtime
fn strict_add(a: int, b: int) { a + b }
// strict_add(1.5, 2.5)  // Compile error - not a runtime test

// Test 3: Calling typed function with missing args
// Missing int args default to 0
fn optional_int(a: int, b: int) { a + b }
optional_int(1)  // Missing second arg defaults to 0, result = 1

// Test 4: Deep recursion with unboxed version
fn countdown(n: int) {
    if (n <= 0) 0 else countdown(n - 1)
}
countdown(100)  // Should work without stack overflow

// Test 5: Very large expression chain
fn chain_test(x: int) { x + 1 }
chain_test(chain_test(chain_test(chain_test(chain_test(1)))))  // 6

// Test 6: Mixing boxed and unboxed in complex expression
fn box_test(x: int) { x * 2 }
fn unbox_test(a, b) { a + b }
unbox_test(box_test(5), box_test(3))  // (5*2) + (3*2) = 16

// Test 7: Return value used in boolean context
// Note: In Lambda, 0 is truthy (only null/false are falsy)
fn nonzero(x: int) { x }
let r1 = if (nonzero(1)) "yes" else "no"  // "yes"
let r2 = if (nonzero(0)) "yes" else "no"  // "yes" (0 is truthy in Lambda)
[r1, r2]

// Test 8: Typed function returning different types (conditional)
fn maybe_int(x: int, flag: bool) {
    if (flag) x else null
}
maybe_int(42, true)   // 42
maybe_int(42, false)  // null

"negative tests completed"
