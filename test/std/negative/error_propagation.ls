// Test: Error Propagation
// Layer: 2 | Category: negative | Covers: error in arithmetic, function calls

// Arithmetic with error
fn fail_fn() int^ { raise error("fail") }
let a = fail_fn() ^ { ^ }
type(a)

// Error + number
let b = fail_fn() ^ { ^ }
type(b + 10)

// Error in array access
let c = fail_fn() ^ { ^ }
type(c)

// Multiple chained errors
fn step1() int^ { raise error("step1 failed") }
fn step2(x: int) int^ { x + 1 }
fn chain() int^ {
    let v = step1()^
    step2(v)^
}
let result = chain() ^ { ^ }
result is error
