// Test structural (deep value) equality for container types

// === List equality ===
[1, 2, 3] == [1, 2, 3]           // true: same elements
[1, 2, 3] == [1, 2, 4]           // false: different element
[1, 2, 3] == [1, 2]              // false: different length
[] == []                          // true: empty lists
["a", "b"] == ["a", "b"]         // true: string elements
[true, false] == [true, false]    // true: bool elements
[null, null] == [null, null]      // true: null elements

// === Nested list equality ===
[[1, 2], [3, 4]] == [[1, 2], [3, 4]]   // true: nested lists
[[1, 2], [3, 4]] == [[1, 2], [3, 5]]   // false: nested difference

// === Numeric promotion in lists ===
[1] == [1.0]                      // true: int vs float promotion
[1, 2] == [1.0, 2.0]             // true: all elements promote

// === Map equality (order-independent) ===
{a: 1, b: 2} == {a: 1, b: 2}    // true: same keys same values
{a: 1, b: 2} == {b: 2, a: 1}    // true: order-independent
{a: 1} == {a: 2}                 // false: different values
{a: 1, b: 2} == {a: 1}          // false: different key count

// === Nested map equality ===
{a: {x: 1}} == {a: {x: 1}}      // true: nested maps
{a: {x: 1}} == {a: {x: 2}}      // false: nested difference

// === Numeric promotion in maps ===
{a: 1} == {a: 1.0}               // true: int vs float in map value

// === Mixed containers ===
{a: [1, 2]} == {a: [1, 2]}       // true: list inside map
{a: [1, 2]} == {a: [1, 3]}       // false: different list in map
[{a: 1}, {b: 2}] == [{a: 1}, {b: 2}]  // true: maps inside list

// === Range equality ===
(1 to 5) == (1 to 5)             // true: same range
(1 to 5) == (1 to 6)             // false: different end

// === Cross-type sequence equality (range vs list/array) ===
(1 to 3) == [1, 2, 3]            // true: range equals list with same elements
[1, 2, 3] == (1 to 3)            // true: symmetric
(1 to 3) == [1, 2, 4]            // false: different element
(1 to 3) == [1, 2]               // false: different length
(1 to 1) == [1]                  // true: single element
(0 to 0) == [0]                  // true: zero range

// === Function reference equality ===
fn add1(x) => x + 1
let f = add1
f == f                            // true: same function reference

// === Inequality operator ===
[1, 2] != [1, 2]                 // false: not-equal of equal lists
[1, 2] != [1, 3]                 // true: not-equal of different lists
{a: 1} != {a: 1}                 // false: not-equal of equal maps
{a: 1} != {a: 2}                 // true: not-equal of different maps

// === Combined results ===
[
  [1, 2] == [1, 2],
  {a: 1} == {a: 1},
  [1] == [1.0],
  {a: 1, b: 2} == {b: 2, a: 1}
]
