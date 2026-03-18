// Test pipe and 'that' spreading in array literals
// Pipe (|) and filter (that) expressions inside array literals
// produce spreadable results that flatten into the enclosing array.

"=== Pipe spread in array literals ==="

// Test 1: Basic identity pipe spread
[1, [2, 3] | ~, 4, 5]

// Test 2: Pipe with transformation
[1, [2, 3] | ~ * 10, 4]

// Test 3: Non-array (scalar) pipe result — pushed normally
fn double(x: int) { x * 2 }
[1, 5 | double, 4]

// Test 4: Pipe with empty array — produces nothing
[1, [] | ~, 4]

// Test 5: Pipe only element
[[3, 4] | ~]

// Test 6: Multiple pipes in same array
[[1, 2] | ~, [3, 4] | ~ * 10]

"=== That/filter spread in array literals ==="

// Test 7: Basic that filter spread
[1, [1, 5, 7, 10, 15] that (~ > 5), 99]

// Test 8: That filter with equality (no parens needed)
[0, [1, 2, 3, 4, 5] that ~ == 3, 9]

// Test 9: That filter removes all — empty result dropped
[1, [10, 20, 30] that (~ > 100), 4]

// Test 10: That filter keeps all
[0, [5, 6, 7] that (~ > 0), 9]

"=== Mixed spreading ==="

// Test 11: For-expr + pipe in same array
[for (x in [1, 2]) x, [3, 4] | ~ * 10]

// Test 12: For-expr + that in same array
[for (x in [10, 20]) x, [1, 2, 3, 4, 5] that (~ > 3)]

// Test 13: Spread + pipe in same array
let a = [100, 200]
[*a, [1, 2] | ~ + 50]

// Test 14: For-expr + pipe + that
[for (x in [1]) x, [10, 20] | ~, [3, 4, 5] that (~ > 3)]

// Test 15: Pipe spread alongside plain values
[0, [1, 2, 3] | ~ + 10, 50, [4, 5] | ~ * 2, 100]

"=== End of pipe spread tests ==="
