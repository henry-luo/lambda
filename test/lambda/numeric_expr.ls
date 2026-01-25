// Final comprehensive numeric operations test - consolidating all edge cases

"===== COMPREHENSIVE NUMERIC OPERATIONS TEST ====="

'Basic Operations:'
5 + 3
5 - 3
5 * 3
10 / 3
10 div 3
2 ^ 3

"Negative Numbers:"
-5 + 3
-5 - 3
-5 * 3
-10 / 3
-10 div 3

"Float Operations:"
5.5 + 2.3
5.5 - 2.3
5.5 * 2.3
5.5 / 2.3
5.5 ^ 2.3

"Mixed Operand Types:"
1 + 0.5
3 - 1.2
4 * 2.5
8 / 2.0
10 div 3.3
0.5 + 7
2.7 - 1
1.5 * 6
9.0 / 3
15.5 div 4

"Modulo Operations (%):"
17 % 5
-17 % 5
17 % -5
-17 % -5
100 % 7
0 % 5
1234567890 % 123

"Modulo by Zero (errors):"
5 % 0
0 % 0
17 % 0

"Modulo with Float (errors):"
5.5 % 3
7 % 2.5
3.14 % 2.71

"Modulo with Boolean/Null (errors):"
true % 2
5 % false
null % 3
7 % null

"Modulo with String (errors):"
"hello" % 3
5 % "test"

"Unary Operations - Fast Path (C operators) - Numeric Types:"
let num = 42
let float_val = 3.14
+num
-num  
+float_val
-float_val

"Unary Operations - Runtime Functions - String to Number Casting:"
let str_int = "123"
let str_float = "3.14"
let str_negative = "-42"
+str_int
-str_int
+str_float
-str_float
+str_negative
-str_negative

"Unary Operations - Runtime Functions - Symbol to Number Casting:"  
let sym_int = '456'
let sym_float = '2.71'
+sym_int
-sym_int
+sym_float
-sym_float

"Unary Operations - Error Cases - Invalid String/Symbol:"
let str_invalid = "hello"
let sym_invalid = 'world'
+str_invalid
-str_invalid
+sym_invalid
-sym_invalid

"Unary Operations - Error Cases - Unsupported Types:"
let bool_val = true
let null_val = null
+bool_val
-bool_val
+null_val
-null_val

"Unary Operations - Mixed Expressions:"
+("42") + -("10")
-("3.14") * +("2")

"Division by Zero:"
5 / 0
0 / 0
5 div 0
0 div 0

"Boolean Arithmetic (errors):"
true + false
true - false
true * false
true / false
true div false
true ^ false

"Null Arithmetic (errors):"
null + 5
5 + null
null * 5
5 * null
null / 5
5 / null
null ^ 5
5 ^ null

"String with Numbers (errors):"
5 + "hello"
"hello" + 5
5 * "test"

"Additional Data Type Errors:"
t'2025-10-01' + 2
5 - t'2024-01-01'
t'2025-08-15' * 2
3 / t'2025-12-31'

"Mixed Type Errors:"
true + 5
5 + true
null + "text"
"text" + null

"More Mixed Type Combinations (errors):"
true * 3.14
2.5 + false
null ^ 3
4 div true
"string" - 10
15 + "text"
t'2025-08-15' + true
false * t'14:30:00'
null / t'2025-01-01T00:00:00'
3.14 ^ "power"
true div false
"hello" ^ 2

"Edge Cases:"
0.000000001 / 1000000
999999999999999999 + 1
-999999999999999999 - 1

"More Edge Cases:"
1e-10 * 1e10
1e15 / 1e-5
0.1 ^ 50
50 ^ 0.1
(-1) ^ 2.5
2.0 / 0.0
-2.0 / 0.0
0.0 / 0.0
1.0 / (-0.0)
1000000000000000000 * 1000000000000000000

"Power Operations:"
2 ^ 0
0 ^ 2
0 ^ 0
2 ^ (-1)
(-2) ^ 3
(-2) ^ 2

"Complex Expressions:"
(5 + 3) * 2
5 + (3 * 2)
(10 / 2) ^ 2

"Operator Precedence Tests:"
1 + 0.7 * 10 / 9.16 - 8
2 * 3 + 4 * 5 - 1
10 - 5 * 2 + 3 / 1.5
4 ^ 2 + 3 * 2 - 1
5 + 3 * 2 ^ 2 - 4 / 2
(1 + 2) * (3 + 4) / (2 - 1)
10 / (2 + 3) * 4 - 1
3 ^ (2 + 1) - 2 * (4 + 1)
1.5 + 2.5 * 3.0 - 4.2 / 1.4
8 * 2 / 4 + 3 - 1 * 2

"Precision Tests:"
0.1 + 0.2
0.3 - 0.1
1.0 / 3.0 * 3.0

"Large Numbers:"
9223372036854775807 + 1
-9223302036854775808 - 1

"Concatenation: "
"hello" ++ " world"
'foo' ++ 'bar' ++ 'baz'

"===== END OF COMPREHENSIVE TEST ====="
