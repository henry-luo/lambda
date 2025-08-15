// Final comprehensive numeric operations test - consolidating all edge cases

"===== COMPREHENSIVE NUMERIC OPERATIONS TEST ====="

"Basic Operations:"
5 + 3
5 - 3
5 * 3
10 / 3
10 _/ 3
2 ^ 3

"Negative Numbers:"
-5 + 3
-5 - 3
-5 * 3
-10 / 3
-10 _/ 3

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
10 _/ 3.3
0.5 + 7
2.7 - 1
1.5 * 6
9.0 / 3
15.5 _/ 4

"Division by Zero:"
5 / 0
0 / 0
5 _/ 0
0 _/ 0

"Boolean Arithmetic (errors):"
true + false
true - false
true * false
true / false
true _/ false
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
"test" * 5

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
4 _/ true
"string" - 10
15 + "text"
t'2025-08-15' + true
false * t'14:30:00'
null / t'2025-01-01T00:00:00'
3.14 ^ "power"
true _/ false
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

"String Repetition:"
"hello" * 3
"a" * 0
5 * "world"

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
-9223372036854775808 - 1

"===== END OF COMPREHENSIVE TEST ====="

"All numeric tests completed successfully."
