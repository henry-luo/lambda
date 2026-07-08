// Test: Cross-Type Comparison
// Layer: 1 | Category: boundary | Covers: <, >, <=, >= across type pairs

let any_int: any = 42
let any_float: any = 3.14
let any_string: any = "hello"
let any_true: any = true
let any_false: any = false
let any_null: any = null

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
(any_int < any_string)
(any_string < any_int)
(any_int < any_true)
(any_true < any_int)
(any_int < any_null) is null
(any_null < any_int) is null
(any_float < any_string)
(any_string < any_float)
(any_float < any_true)
(any_float < any_null) is null
(any_string < any_true)
(any_string < any_null) is null
(any_true < any_false)
(any_null < any_null) is null

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
(any_int > any_string)
(any_int > any_true)
(any_int > any_null) is null
(any_float > any_null) is null
(any_true > any_false)
(any_null > any_null) is null

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
(any_int <= any_string)
(any_int <= any_true)
(any_int <= any_null) is null

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
(any_int >= any_string)
(any_int >= any_true)
(any_int >= any_null) is null
