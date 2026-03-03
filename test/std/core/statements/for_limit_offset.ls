// Test: For with Limit and Offset
// Layer: 3 | Category: statement | Covers: for limit, offset clauses

[for (x in [1, 2, 3, 4, 5] limit 3) x]
[for (x in [1, 2, 3, 4, 5] offset 2) x]
[for (x in [1, 2, 3, 4, 5] limit 2 offset 1) x]
[for (x in [1, 2, 3] limit 0) x]
[for (x in [1, 2, 3] offset 10) x]
[for (x in [1, 2, 3] limit 100) x]
