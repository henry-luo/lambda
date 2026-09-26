// Test: Set Functions
// Layer: 2 | Category: operator | Covers: intersect, unique, except
// S10.1.1v2: `|`, `&` and `!` are type operators only; set algebra on
// containers is intersect(a, b, ...), unique(a, b, ...) and except(a, b).

// ===== Array intersection =====
intersect([1, 2, 3, 4], [3, 4, 5, 6]);
intersect([1, 2, 3], [4, 5, 6]);
intersect([1, 2, 3], [1, 2, 3]);
intersect([], [1, 2, 3]);

// ===== Array union =====
unique([1, 2, 3], [3, 4, 5]);
unique([1, 2], [3, 4]);
unique([1, 2, 3], []);
unique([], [1, 2, 3]);

// ===== Array exclusion =====
except([1, 2, 3, 4, 5], [2, 4]);
except([1, 2, 3], [1, 2, 3]);
except([1, 2, 3], []);
except([], [1, 2, 3]);

// ===== String set operations =====
intersect(["a", "b", "c"], ["b", "c", "d"]);
unique(["a", "b"], ["b", "c"]);
except(["a", "b", "c"], ["b"]);

// ===== Chained set operations =====
except(intersect([1, 2, 3, 4, 5], [2, 3, 4, 5, 6]), [4])
