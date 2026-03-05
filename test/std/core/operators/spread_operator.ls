// Test: Spread Operator
// Layer: 2 | Category: operator | Covers: *expr in arrays, lists, function calls

// ===== Spread array into array =====
let a = [1, 2, 3]
[0, *a, 4]

// ===== Spread into list =====
let b = [4, 5, 6]
(1, 2, 3, *b, 7, 8)

// ===== Spread list into array =====
let c = (10, 20, 30)
[0, *c, 40]

// ===== Multiple spreads =====
let e = [1, 2]
let f = [3, 4]
[*e, *f]

// ===== Spread empty array =====
let empty = []
[1, *empty, 2]

// ===== Spread in nested context =====
let inner = [2, 3]
[[1, *inner, 4], [5, 6]]

// ===== Spread with for-expression =====
let nums = [1, 2, 3]
[for (x in nums) x * 2, *[100, 200]]

// ===== Spread in function call =====
fn concat_arrays(a, b) {
    [*a, *b]
}
concat_arrays([1, 2], [3, 4])

// ===== Spread function result =====
fn make_array() {
    [10, 20, 30]
}
[0, *make_array(), 40]

// ===== Spreadable for in array =====
[0, for (x in [1, 2]) x * 5, 99]

// ===== Spreadable for in list =====
(0, for (x in [1, 2]) x * 5, 99)
