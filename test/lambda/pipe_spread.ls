// Pipe and '|:' filter results in array literals (S10.1.2v4, S10.1.6, S2.5.1v2,
// S2.5.7v2). A pipe or filter result is placed as a value: a sequence source,
// a list included, gives an array, which stays one item; a scalar source gives
// its one value.

"=== Pipe over an array: one item ===";

// Test 1: Basic identity pipe
[1, [2, 3] |> ~, 4, 5];

// Test 2: Pipe with transformation
[1, [2, 3] |> ~ * 10, 4]

// Test 3: Non-array (scalar) pipe result — pushed normally
fn double(x: int) { x * 2 }
[1, 5 |> double, 4];

// Test 4: Pipe over an empty array is `[]`
[1, [] |> ~, 4];

// Test 5: Pipe only element
[[3, 4] |> ~];

// Test 6: Multiple pipes in same array
[[1, 2] |> ~, [3, 4] |> ~ * 10]

"=== Filter over an array: one item ===";

// Test 7: Basic |: filter
[1, [1, 5, 7, 10, 15] |: (~ > 5), 99];

// Test 8: Filter with equality (no parens needed)
[0, [1, 2, 3, 4, 5] |: ~ == 3, 9];

// Test 9: A filter that removes all is `[]` (S10.1.6)
[1, [10, 20, 30] |: (~ > 100), 4];

// Test 10: A filter that keeps all
[0, [5, 6, 7] |: (~ > 0), 9]

"=== Mixed ===";

// Test 11: For-expr + pipe in same array: the for-expression spreads
[for (x in [1, 2]) x, [3, 4] |> ~ * 10];

// Test 12: For-expr + |: in same array
[for (x in [10, 20]) x, [1, 2, 3, 4, 5] |: (~ > 3)]

// Test 13: Spread + pipe in same array
let a = [100, 200];
[*a, [1, 2] |> ~ + 50];

// Test 14: For-expr + pipe + |:
[for (x in [1]) x, [10, 20] |> ~, [3, 4, 5] |: (~ > 3)];

// Test 15: Pipe results alongside plain values
[0, [1, 2, 3] |> ~ + 10, 50, [4, 5] |> ~ * 2, 100]

"=== Pipe over a list: one item (S2.5.7v2) ===";

// Test 16: a list maps to an array, which stays one item
[1, (2, 3) |> ~ * 10, 4];

// Test 17: a list filters to an array, which stays one item
[0, (1, 5, 7, 10) |: (~ > 4), 99];

// Test 18: one item left is `[5]` and none is `[]` -- a filter never collapses (S10.1.6)
[0, (1, 5) |: (~ > 4), 9];
[0, (1, 2) |: (~ > 4), 9]

"=== End of pipe spread tests ==="
