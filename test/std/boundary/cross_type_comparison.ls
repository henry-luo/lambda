// Test: Cross-Type Comparison
// Layer: 1 | Category: boundary | Covers: <, >, <=, >= across type pairs

// ===== Less Than (<) =====
// Same-type numeric
(1 < 2)
(2 < 1)
(1 < 1)
// Int × Float promotion
(42 < 43.0)
(42 < 41.0)
(42 < 42.0)
// Float × Int promotion
(3.14 < 4)
(3.14 < 3)
// String lexicographic
("abc" < "def")
("def" < "abc")
("abc" < "abc")
("a" < "ab")
("" < "a")
// Invalid cross-type < comparisons
(42 < "hello")
("hello" < 42)
(42 < true)
(true < 42)
(42 < null)
(null < 42)
(3.14 < "hello")
("hello" < 3.14)
(3.14 < true)
(3.14 < null)
("hello" < true)
("hello" < null)
(true < false)
(null < null)

// ===== Greater Than (>) =====
// Same-type numeric
(2 > 1)
(1 > 2)
(1 > 1)
// Int × Float
(43 > 42.0)
(41 > 42.0)
// Float × Int
(4.0 > 3)
(2.0 > 3)
// String
("def" > "abc")
("abc" > "def")
// Invalid cross-type >
(42 > "hello")
(42 > true)
(42 > null)
(3.14 > null)
(true > false)
(null > null)

// ===== Less Than or Equal (<=) =====
(1 <= 2)
(2 <= 1)
(1 <= 1)
(42 <= 42.0)
(42 <= 43.0)
("abc" <= "abc")
("abc" <= "def")
("def" <= "abc")
// Invalid
(42 <= "hello")
(42 <= true)
(42 <= null)

// ===== Greater Than or Equal (>=) =====
(2 >= 1)
(1 >= 2)
(1 >= 1)
(42.0 >= 42)
(43.0 >= 42)
("abc" >= "abc")
("def" >= "abc")
("abc" >= "def")
// Invalid
(42 >= "hello")
(42 >= true)
(42 >= null)
