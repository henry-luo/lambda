// Test: That Operator (Constraint/Filter)
// Layer: 2 | Category: operator | Covers: that as type constraint and pipe filter

// ===== Type-level constraint =====
type Positive = int that (~ > 0)
(5 is Positive)
(-1 is Positive)
(0 is Positive)
(100 is Positive)

// ===== Range constraint =====
type Between5And10 = int that (5 < ~ < 10)
(6 is Between5And10)
(7 is Between5And10)
(5 is Between5And10)
(10 is Between5And10)
(3 is Between5And10)

// ===== Combined constraint =====
type InRange = int that (~ >= 1 and ~ <= 10)
(1 is InRange)
(5 is InRange)
(10 is InRange)
(0 is InRange)
(11 is InRange)

// ===== Constrained types in match =====
fn classify(x) => match x {
    case int that (~ > 0): "positive"
    case int that (~ < 0): "negative"
    case 0: "zero"
    default: "other"
}
classify(5)
classify(-3)
classify(0)
classify("hi")

// ===== Match with range constraints =====
fn grade(score) => match score {
    case int that (90 <= ~ <= 100): "A"
    case int that (80 <= ~ < 90): "B"
    case int that (70 <= ~ < 80): "C"
    case int that (60 <= ~ < 70): "D"
    case int that (0 <= ~ < 60): "F"
    default: "invalid"
}
grade(95)
grade(85)
grade(75)
grade(65)
grade(55)

// ===== that as filter (same as where) =====
[1, 2, 3, 4, 5] that (~ > 3)
[1, 2, 3, 4, 5, 6] that (~ % 2 == 0)

// ===== Combined results =====
[
    (7 is Between5And10),
    (5 is Between5And10),
    (1 is Positive),
    (-1 is Positive),
    (1 is InRange),
    (11 is InRange)
]
