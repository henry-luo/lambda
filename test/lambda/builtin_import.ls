// Test built-in module import: no import - use package prefix
// This is the default behavior (math.sqrt, math.sin, etc.)

"1. math functions with prefix"
math.sqrt(16)
math.sin(0)
math.cos(0)
[math.log(1), math.exp(0), math.pow(2, 3)]

"2. math constants with prefix"
math.pi
math.e

"3. statistics with prefix"
math.mean([2, 4, 6])
math.variance([2, 4, 6])
