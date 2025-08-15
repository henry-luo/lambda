// Comprehensive comparison operations test - all comparison operators with various types

"===== COMPREHENSIVE COMPARISON OPERATIONS TEST ====="

"Basic Equality Comparisons (==):"
(5 == 5)
(5 == 3)
(3.14 == 3.14)
(3.14 == 2.71)
(5 == 5.0)
(5.0 == 5)
(-5 == -5)
(0 == 0)
(0.0 == 0.0)
(-0.0 == 0.0)

"Basic Inequality Comparisons (!=):"
(5 != 3)
(5 != 5)
(3.14 != 2.71)
(3.14 != 3.14)
(5 != 5.1)
(5.0 != 5)
(-5 != -3)
(0 != 1)
(0.0 != 0.1)

"Less Than Comparisons (<):"
(3 < 5)
(5 < 3)
(5 < 5)
(3.14 < 3.15)
(3.15 < 3.14)
(3.0 < 3.0)
(-5 < -3)
(-3 < -5)
(0 < 1)
(-1 < 0)
(5 < 5.1)
(5.1 < 5)

"Greater Than Comparisons (>):"
(5 > 3)
(3 > 5)
(5 > 5)
(3.15 > 3.14)
(3.14 > 3.15)
(3.0 > 3.0)
(-3 > -5)
(-5 > -3)
(1 > 0)
(0 > -1)
(5.1 > 5)
(5 > 5.1)

"Less Than or Equal Comparisons (<=):"
(3 <= 5)
(5 <= 3)
(5 <= 5)
(3.14 <= 3.15)
(3.15 <= 3.14)
(3.14 <= 3.14)
(-5 <= -3)
(-3 <= -5)
(-5 <= -5)
(0 <= 1)
(-1 <= 0)
(0 <= 0)
(5 <= 5.1)
(5.1 <= 5)
(5.0 <= 5)

"Greater Than or Equal Comparisons (>=):"
(5 >= 3)
(3 >= 5)
(5 >= 5)
(3.15 >= 3.14)
(3.14 >= 3.15)
(3.14 >= 3.14)
(-3 >= -5)
(-5 >= -3)
(-5 >= -5)
(1 >= 0)
(0 >= -1)
(0 >= 0)
(5.1 >= 5)
(5 >= 5.1)
(5 >= 5.0)

"Logical NOT Operations (not):"
not true
not false
not (5 == 5)
not (5 == 3)
not (3 < 5)
not (5 < 3)
not (5 <= 5)
not (5 > 3)

"Logical AND Operations (and):"
(true and true)
(true and false)
(false and true)
(false and false)
((5 == 5) and (3 < 7))
((5 == 3) and (3 < 7))
((5 != 3) and (7 > 2))
((5 > 3) and (5 <= 5))
((3 < 5) and (10 >= 10))
(not (5 == 3) and (7 > 2))

"Logical OR Operations (or):"
(true or true)
(true or false)
(false or true)
(false or false)
((5 == 5) or (3 > 7))
((5 == 3) or (3 < 7))
((5 != 3) or (7 < 2))
((5 < 3) or (5 >= 5))
((3 > 5) or (10 <= 10))
(not (5 == 5) or (7 > 2))

"Mixed Boolean Logic:"
(true and (false or true))
(false or (true and false))
((true or false) and (true or false))
(not (false and true) or (false or true))
((5 == 5) and ((3 < 7) or (10 > 5)))
((5 != 5) or ((3 >= 7) and (10 <= 5)))

"Logical Operations with Mixed Types:"
(5 and true)
(0 and true)
(true and 5)
(true and 0)
(5 or false)
(0 or false)
(false or 5)
(false or 0)

"Large Number Comparisons:"
(999999999 == 999999999)
(999999999 != 999999998)
(999999999 > 999999998)
(999999998 < 999999999)
(1000000000 >= 1000000000)
(1000000000 <= 1000000000)

"Negative Number Comparisons:"
(-999 == -999)
(-999 != -998)
(-999 < -998)
(-998 > -999)
(-1000 <= -999)
(-999 >= -1000)

"Zero Comparisons:"
(0 == 0)
(0 != 1)
(0 < 1)
(1 > 0)
(0 <= 0)
(0 >= 0)
(-0 == 0)
(0.0 == 0)
(0 == 0.0)

"Mixed Type Comparisons:"
(5 == 5.0)
(5.0 == 5)
(5 != 5.1)
(5.1 != 5)
(5 < 5.1)
(5.1 > 5)
(5 <= 5.0)
(5.0 >= 5)

"Complex Boolean Expressions:"
((5 == 5) and (3 < 7))
((5 != 3) or (10 < 5))
(not (5 == 3) and (7 > 2))
((5 > 3) and not (2 > 5))
(((5 == 5) and (3 < 7)) or (10 != 10))

"Nested Comparisons:"
(5 < 10) == (3 < 7)
(5 > 10) != (3 < 7)
(5 <= 5) == (10 >= 10)
not ((5 < 3) or (10 > 20))

"Error Cases - Type Mismatches (should produce errors):"
"Testing invalid comparisons..."
("hello" < 5)
(true >= false)
(null == 3)
(5 > "world")
(false <= true)
("test" != null)

"Additional Error Cases - Invalid Type Comparisons:"
(true == 1)
(false == 0)
(1 == true)
(0 == false)
(true != 1)
(false != 0)
("string" == true)
("string" == false)
(true == "string")
(false == "string")

"Error Cases - String Comparisons (non-equality):"
("hello" < "world")
("abc" > "def")
("test" <= "other")
("first" >= "second")

"Error Cases - Boolean with String Operations:"
("str" and true)
("str" and false)
(true and "str")
(false and "str")
("str" or true)
("str" or false)
(true or "str")
(false or "str")
("hello" and "world")

"Error Cases - Null Comparisons:"
(null < 5)
(null > 5)
(null <= 5)
(null >= 5)
(5 < null)
(5 > null)
(5 <= null)
(5 >= null)
(null < null)
(null > null)
(null <= null)
(null >= null)

"Error Cases - Null with Boolean Operations:"
(null and true)
(null and false)
(true and null)
(false and null)
(null or true)
(null or false)
(true or null)
(false or null)
(null and null)
(null or null)

"Error Cases - Mixed Invalid Types:"
(true < false)
(false > true)
(true <= false)
(false >= true)
("string" and 5)
(5 and "string")
("string" or 5)
(5 or "string")
(true and 3.14)
(3.14 and true)
(false or 2.71)
(2.71 or false)

"Conditional Expressions with Comparisons:"
if (5 > 3) "five is greater" else "five is not greater"
if (5 == 5.0) "equal" else "not equal"
if (not (5 < 3)) "not less than" else "less than"
if ((5 <= 5) and (10 >= 10)) "both true" else "not both true"

"Variable Comparisons:"
let x = 10
let y = 20  
let z = 10.0

(x == 10)
(y > x)
(x == z)
(x < y)
(y >= x)
(not (x > y))
((x == z) and (y > x))

"Complex Arithmetic in Comparisons:"
((5 + 3) == 8)
((10 - 2) > 7)
((3 * 4) <= 12)
((15 / 3) == 5)
(not ((2 ^ 3) != 8))

"Chained Comparisons Logic:"
((5 < 10) and (10 < 15))
((5 <= 10) and (10 <= 10))
(not (5 > 10) or (15 > 10))
(((5 == 5) and (10 != 5)) and (15 > 10))

"Edge Cases with Very Large Numbers:"
(9223372036854775807 == 9223372036854775807)
(9223372036854775806 < 9223372036854775807)
(-9223372036854775808 < -9223372036854775807)

"Float Precision Comparisons:"
((0.1 + 0.2) == 0.3)
((1.0 / 3.0) != 0.33333)
(3.14159 > 3.14158)
(2.718281828 <= 2.718281829)

"===== TEST SUMMARY ====="
"Total expressions tested: ~200+ comparison operations"
"Categories covered:"
"- Basic equality and inequality (==, !=)"
"- Relational operators (<, >, <=, >=)" 
"- Logical NOT operations (not)"
"- Logical AND/OR operations (and, or)"
"- Mixed type comparisons (int/float)"
"- Complex boolean expressions"
"- Error cases with invalid types"
"- Boolean-numeric type mismatch errors (true==1, false==0, etc.)"
"- String comparison errors (non-equality operators)"
"- Boolean-string operation errors"
"- Null comparison and operation errors"
"- Mixed invalid type combinations"
"- Edge cases and precision tests"
