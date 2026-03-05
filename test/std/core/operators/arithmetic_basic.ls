// Test: Arithmetic Basic
// Layer: 1 | Category: operator | Covers: +, -, *, / with int and float

// ===== Integer arithmetic =====
3 + 4
10 - 7
6 * 8
15 / 3
100 / 7
0 + 0
0 * 100

// ===== Float arithmetic =====
1.5 + 2.5
10.0 - 3.5
2.5 * 4.0
10.0 / 3.0

// ===== Mixed int/float =====
3 + 2.5
10 - 0.5
4 * 2.5
10 / 3.0

// ===== Negative numbers =====
-5 + 3
3 + (-5)
-3 * -4
-10 / 2

// ===== Chained arithmetic =====
1 + 2 + 3 + 4
10 - 3 - 2
2 * 3 * 4
100 / 10 / 2

// ===== Order of operations =====
2 + 3 * 4
(2 + 3) * 4
10 - 2 * 3
10 / 2 + 3

// ===== Vector arithmetic =====
[1, 2, 3] + [4, 5, 6]
[1, 2, 3] * 2
[10, 20, 30] - [1, 2, 3]
