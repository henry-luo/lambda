// Test: Set Operators
// Layer: 2 | Category: operator | Covers: & intersection, | union, ! exclusion

// ===== Array intersection =====
[1, 2, 3, 4] & [3, 4, 5, 6]
[1, 2, 3] & [4, 5, 6]
[1, 2, 3] & [1, 2, 3]
[] & [1, 2, 3]

// ===== Array union =====
[1, 2, 3] | [3, 4, 5]
[1, 2] | [3, 4]
[1, 2, 3] | []
[] | [1, 2, 3]

// ===== Array exclusion =====
[1, 2, 3, 4, 5] ! [2, 4]
[1, 2, 3] ! [1, 2, 3]
[1, 2, 3] ! []
[] ! [1, 2, 3]

// ===== String set operations =====
["a", "b", "c"] & ["b", "c", "d"]
["a", "b"] | ["b", "c"]
["a", "b", "c"] ! ["b"]

// ===== Chained set operations =====
([1, 2, 3, 4, 5] & [2, 3, 4, 5, 6]) ! [4]
