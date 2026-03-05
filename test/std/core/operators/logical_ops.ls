// Test: Logical Operators
// Layer: 1 | Category: operator | Covers: and, or, not with short-circuit

// ===== Basic and =====
(true and true)
(true and false)
(false and true)
(false and false)

// ===== Basic or =====
(true or true)
(true or false)
(false or true)
(false or false)

// ===== Basic not =====
(not true)
(not false)

// ===== Short-circuit: or returns first truthy =====
(1 or 2)
(null or "default")
(false or "fallback")
(null or false or "last")

// ===== Short-circuit: and returns first falsy =====
(1 and 2)
(null and "never")
(false and "never")

// ===== Lambda truthiness in logic =====
(0 or "default")
([] or "default")
({} or "default")
("" or "default")
(error("x") or "default")

// ===== Combined logic =====
(true and true or false)
(false or true and true)
(not false and true)
(not (true and false))

// ===== Logic with comparisons =====
(1 < 2 and 3 < 4)
(1 > 2 or 3 < 4)
(not (1 > 2))
