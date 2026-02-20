// Test: Error Propagation
// Layer: 2 | Category: negative | Covers: error in arithmetic, function calls

// Arithmetic with error
fn fail_fn() int^ { raise error("fail") }
let a^err1 = fail_fn()
type(err1)

// Error + number
let b^err2 = fail_fn()
type(err2 + 10)

// Error in array access
let c^err3 = fail_fn()
type(err3)

// Multiple chained errors
fn step1() int^ { raise error("step1 failed") }
fn step2(x: int) int^ { x + 1 }
fn chain() int^ {
    let v = step1()^
    step2(v)^
}
let result^err4 = chain()
^err4
