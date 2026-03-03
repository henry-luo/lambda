// Test: For with Order By
// Layer: 3 | Category: statement | Covers: for order by clause

[for (x in [3, 1, 4, 1, 5] order by x) x]
[for (x in [3, 1, 4, 1, 5] order by x desc) x]
[for (x in [10, 5, 15, 20, 25, 3], let sq = x * x where x > 5 order by sq desc limit 2) sq]
[for (x in [1, 2, 3, 4, 5] order by x desc limit 3) x]
[for (x in [5, 2, 8, 1, 9, 3] order by x limit 3 offset 1) x]
