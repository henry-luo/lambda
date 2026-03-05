// Test: Comparison Operators
// Layer: 1 | Category: operator | Covers: ==, !=, <, >, <=, >=

// ===== Integer comparison =====
(1 == 1)
(1 != 2)
(1 < 2)
(2 > 1)
(3 <= 3)
(3 >= 3)
(3 <= 4)
(4 >= 3)

// ===== Float comparison =====
(3.14 == 3.14)
(3.14 != 2.71)
(1.5 < 2.5)
(2.5 > 1.5)

// ===== String comparison =====
("abc" == "abc")
("abc" != "def")
("abc" < "def")
("def" > "abc")
("abc" <= "abc")

// ===== Int-Float comparison =====
(42 == 42.0)
(0 == 0.0)
(42 != 42.5)

// ===== Null comparison =====
(null == null)
(null == false)
(null == 0)
(null != 1)

// ===== Bool comparison =====
(true == true)
(false == false)
(true != false)

// ===== Symbol comparison =====
('abc' == 'abc')
('abc' != 'def')
('abc' < 'def')

// ===== Collection comparison =====
([1, 2] == [1, 2])
([1, 2] != [1, 3])
({a: 1} == {a: 1})
