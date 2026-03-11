// Test: For Expression Comprehensive
// Layer: 3 | Category: statement | Covers: for with all clauses

[for (x in [1, 2, 3]) x * 2]
[for (x in [1, 2, 3, 4, 5] where x > 2) x]
[for (x in [3, 1, 4, 1, 5] order by x) x]
[for (x in [3, 1, 4, 1, 5] order by x desc) x]
[for (i, v in [10, 20, 30]) i]
[for (x in 1 to 5) x ** 2]
[for (x in []) x]
[for (k, v in {a: 1, b: 2}) k]
