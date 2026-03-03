// Test: For with Let Clause
// Layer: 3 | Category: statement | Covers: for with let binding

[for (x in [1, 2, 3], let y = x * 2) y]
[for (x in [1, 2, 3, 4, 5], let sq = x ** 2 where sq > 5) sq]
[for (x in [1, 2, 3], let doubled = x * 2, let tripled = x * 3) doubled + tripled]
[for (x in [5, 3, 1, 4, 2], let sq = x ** 2 where sq > 5 order by sq) sq]
