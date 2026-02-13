// Pipe and Where Operator Test Suite
// Tests the pipe (|) and where operators with ~ (current item) and ~# (current index)

"===== PIPE OPERATOR BASIC TESTS ====="

"--- Array iteration with ~ ---"

// Basic array pipe: multiply each element by 2
[1, 2, 3] | ~ * 2

// Array pipe with addition
[10, 20, 30] | ~ + 5

// Array pipe with division
[10, 20, 30] | ~ / 2

// Array pipe with subtraction
[5, 10, 15] | ~ - 1

"--- Array iteration with ~# (index) ---"

// Access index during iteration
[100, 200, 300] | ~#

// Use both item and index
["a", "b", "c"] | { item: ~, index: ~# }

// Multiply item by its index
[10, 10, 10] | ~ * ~#

"--- Map iteration with ~ ---"

// Map pipe: transform values
{ x: 1, y: 2, z: 3 } | ~ * 10

// Map pipe: access keys during iteration
{ a: 100, b: 200, c: 300 } | ~#

// Map pipe: create new structure
{ a: 1, b: 2 } | { key: ~#, value: ~ }

"===== WHERE OPERATOR BASIC TESTS ====="

"--- Filter arrays ---"

// Filter even numbers
[1, 2, 3, 4, 5, 6] that (~ % 2 == 0)

// Filter numbers greater than 2
[1, 2, 3, 4, 5] that (~ > 2)

// Filter numbers less than 4
[1, 2, 3, 4, 5] that (~ < 4)

// Filter by equality
[1, 2, 3, 2, 1] that (~ == 2)

// Filter negative condition
[1, 2, 3, 4, 5] that (~ != 3)

"--- Filter arrays with index ---"

// Filter by index (even indices)
["a", "b", "c", "d", "e"] that (~# % 2 == 0)

// Filter by index (first 3 items)
[100, 200, 300, 400, 500] that (~# < 3)

"--- Filter maps ---"

// Filter map entries by value
{ a: 1, b: 5, c: 2, d: 8 } that (~ > 3)

"===== CHAINED PIPE AND WHERE ====="

"--- Pipe after pipe ---"

// Double transformation
[1, 2, 3] | ~ + 1 | ~ * 2

// Triple transformation
[1, 2, 3] | ~ * 2 | ~ + 10 | ~ / 2

"--- Where after pipe ---"

// Transform then filter
[1, 2, 3, 4, 5] | ~ * 2 that (~ > 5)

"--- Pipe after where ---"

// Filter then transform
[1, 2, 3, 4, 5] that (~ > 2) | ~ * 10

"--- Complex chains ---"

// Multiple filter and transform operations
[1, 2, 3, 4, 5, 6, 7, 8, 9, 10] that (~ % 2 == 0) | ~ * 3 that (~ > 10)

"===== PIPE WITH COMPLEX EXPRESSIONS ====="

"--- Pipe creating maps ---"

// Create array of maps
[1, 2, 3] | { value: ~, doubled: ~ * 2 }

// Include index in map
["x", "y", "z"] | { index: ~#, name: ~ }

"--- Pipe with nested structures ---"

// Accessing nested data
[{ x: 1 }, { x: 2 }, { x: 3 }] | ~.x

// Transforming nested values
[{ x: 1 }, { x: 2 }, { x: 3 }] | ~.x * 2

"===== EDGE CASES ====="

"--- Empty collections ---"

// Empty array pipe
[] | ~ * 2

// Empty array where
[] that (~ > 0)

"--- Single element ---"

// Single element array pipe
[42] | ~ * 2

// Single element that (matches)
[5] that (~ > 0)

// Single element that (no match)
[5] that (~ > 10)

// Single key map pipe
{ only: 100 } | ~ + 1

"--- All elements filtered ---"

// Where returns empty when nothing matches
[1, 2, 3] that (~ > 100)

"--- No elements filtered ---"

// Where returns all when everything matches
[1, 2, 3] that (~ >= 1)

"===== END PIPE AND WHERE TESTS ====="
