// Test: If Expression
// Layer: 3 | Category: statement | Covers: if/else, nested, conditions

(if (true) 1 else 0)
(if (false) 1 else 0)
(if (5 > 3) 42 else -1)
(if (3 > 5) 42 else -1)
(if (true) (if (true) 1 else 2) else 3)
(if (false) 1 else (if (true) 2 else 3))
(let x = 85, if (x >= 90) "A" else if (x >= 80) "B" else if (x >= 70) "C" else "F")
(if (null) 1 else 0)
(if (0) 1 else 0)
