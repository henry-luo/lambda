// Test spread operator: *expr
// Spreads items from arrays/lists into containers

"=== Spread operator tests ==="

// Test 1: Spread array into array
let a = [1, 2, 3];
[0, *a, 4]

// Test 2: Spread array into list
let b = [4, 5, 6];
(1, 2, 3, *b, 7, 8)

// Test 3: Spread list into array
let c = (10, 20, 30);
[0, *c, 40]

// Test 4: Spread list into list
let d = (100, 200);
(50, *d, 300)

// Test 5: Multiple spreads in same container
let e = [1, 2];
let f = [3, 4];
[*e, *f]

// Test 6: Spread empty array
let empty = [];
[1, *empty, 2]

// Test 7: Spread in nested context
let inner = [2, 3];
[[1, *inner, 4], [5, 6]]

// Test 8: Spread with for-expression
let nums = [1, 2, 3];
[for (x in nums) x * 2, *[100, 200]]

// Test 9: Spread variable in function
fn concat_arrays(a, b) {
    [*a, *b]
}
concat_arrays([1, 2], [3, 4])

// Test 10: Spread result of function call
fn make_array() {
    [10, 20, 30]
}
[0, *make_array(), 40]

"=== End of spread operator tests ==="
