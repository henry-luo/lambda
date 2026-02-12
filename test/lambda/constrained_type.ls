// Test constrained types with where clause

// Basic integer range constraint
type between5and10 = int where (5 < ~ < 10)
(6 is between5and10)     // expected: true
(7 is between5and10)     // expected: true
(9 is between5and10)     // expected: true
(5 is between5and10)     // expected: false (not > 5)
(10 is between5and10)    // expected: false (not < 10)
(3 is between5and10)     // expected: false
(15 is between5and10)    // expected: false

// Positive integer constraint
type positive = int where (~ > 0)
(1 is positive)          // expected: true
(100 is positive)        // expected: true
(0 is positive)          // expected: false
(-1 is positive)         // expected: false

// Constraint with <= and >=
type between1and10 = int where (~ >= 1 and ~ <= 10)
(1 is between1and10)     // expected: true
(5 is between1and10)     // expected: true
(10 is between1and10)    // expected: true
(0 is between1and10)     // expected: false
(11 is between1and10)    // expected: false

// Combined result
[
  (7 is between5and10),   // true
  (5 is between5and10),   // false
  (1 is positive),        // true
  (-1 is positive),       // false
  (1 is between1and10),   // true
  (11 is between1and10)   // false
]

// Constrained types in match case expressions
fn classify(x) => match x {
  case int where (~ > 0): "positive"
  case int where (~ < 0): "negative"
  case 0: "zero"
  default: "other"
}

classify(5)      // expected: "positive"
classify(-3)     // expected: "negative"
classify(0)      // expected: "zero"
classify("hi")   // expected: "other"

// Match with range constraints
fn grade(score) => match score {
  case int where (90 <= ~ <= 100): "A"
  case int where (80 <= ~ < 90): "B"
  case int where (70 <= ~ < 80): "C"
  case int where (60 <= ~ < 70): "D"
  case int where (0 <= ~ < 60): "F"
  default: "invalid"
}

grade(95)    // expected: "A"
grade(85)    // expected: "B"
grade(75)    // expected: "C"
grade(65)    // expected: "D"
grade(55)    // expected: "F"
grade(-5)    // expected: "invalid"

// Combined match results
[
  classify(5),
  classify(-3),
  classify(0),
  grade(95),
  grade(75),
  grade(55)
]
