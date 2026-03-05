// Test: Type Basic
// Layer: 1 | Category: datatype | Covers: first-class types, type(), type alias

// ===== type() function =====
type(42)
type(3.14)
type("hello")
type(true)
type(null)
type([1, 2])
type({a: 1})
type(<div>)
type(1 to 5)

// ===== Type alias =====
type Positive = int that (~ > 0)
(5 is Positive)
(-1 is Positive)

// ===== Union type =====
type StringOrInt = string | int
(42 is StringOrInt)
("hello" is StringOrInt)
(true is StringOrInt)

// ===== Type occurrence =====
type TwoInts = int[2]
([1, 2] is TwoInts)
([1] is TwoInts)
([1, 2, 3] is TwoInts)

type MaybeInt = int?
(null is MaybeInt)
(42 is MaybeInt)

type OneOrMore = int+
([1, 2] is OneOrMore)
([] is OneOrMore)

type ZeroOrMore = int*
([] is ZeroOrMore)
([1, 2, 3] is ZeroOrMore)

// ===== is checks =====
(42 is int)
(42 is float)
(42 is number)
("hello" is string)
(true is bool)

// ===== is not =====
(42 is string)
("hello" is int)
