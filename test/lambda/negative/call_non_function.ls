// Negative test: calling non-function values
// These should produce errors, not crashes

// Test 1: Semi-colon creating a list instead of returning function
// This was the actual crash case found by fuzzing
fn make_counter(start: int) {
    fn count(step: int) => start + step;6
    count
}

// Calling make_counter returns [6, fn] instead of fn
// Then trying to call it crashes
let counter = make_counter(100);
counter(5)

// Test 2: Calling a number
let num = 42;
// num(5)  // Would error: calling non-function

// Test 3: Calling a string
let str = "hello";
// str(1)  // Would error: calling non-function

// Test 4: Calling a list
let lst = [1, 2, 3];
// lst(0)  // Would error: calling non-function
