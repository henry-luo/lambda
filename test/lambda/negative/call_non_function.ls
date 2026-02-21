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

// Test 2: Operator mutation creating ItemError then calling it
// Found by fuzzy testing: make_multiplier/(3) returns ItemError from division
// Then calling ItemError as function crashes
fn mult(x) => x * 2;
let triple = mult / 3;  // returns ItemError (can't divide function by int)
triple(5)  // Would crash: calling ItemError as function

// Test 3: Power operator mutation
fn add(x) => x + 1;
let bad = add ** 2;  // returns ItemError (can't power function)
bad(5)  // Would crash: calling ItemError as function
