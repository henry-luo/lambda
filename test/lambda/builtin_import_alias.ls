// Test built-in module aliased import: import m:math;
// Use alias prefix m. instead of math.
import m:math;

"1. math functions with alias"
m.sqrt(16)
m.sin(0)
m.cos(0)
[m.log(1), m.exp(0), m.pow(2, 3)]

"2. math constants with alias"
m.pi
m.e

"3. statistics with alias"
m.mean([2, 4, 6])
m.variance([2, 4, 6])

"4. trigonometric with alias"
[m.asin(0), m.acos(1), m.atan(0)]

"5. use in expressions"
2 * m.pi
m.sqrt(2) * m.sqrt(2)
