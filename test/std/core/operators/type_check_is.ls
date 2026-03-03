// Test: Type Check (is) Operator
// Layer: 3 | Category: operator | Covers: is operator

42 is int
3.14 is float
true is bool
null == null
"hello" is string
'sym' is symbol
[1, 2] is array
{a: 1} is map
42 is number
3.14 is number
not (42 is string)
not ("hello" is int)
not (null is int)
42 is string
"hello" is int
