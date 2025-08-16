// Comprehensive test for numeric system functions

// abs() function tests
abs(-5)
abs(-5) == 5
abs(5)
abs(5) == 5
abs(-3.14)
abs(-3.14) == 3.14
abs(3.14)
abs(3.14) == 3.14
abs(0)
abs(0) == 0

// round() function tests  
round(3.14)
round(3.14) == 3.0
round(3.64)
round(3.64) == 4.0
round(-3.14)
round(-3.14) == -3.0
round(-3.64)
round(-3.64) == -4.0
round(5)
round(5) == 5

// floor() function tests
floor(3.14)
floor(3.14) == 3.0
floor(3.99)
floor(3.99) == 3.0
floor(-3.14)
floor(-3.14) == -4.0
floor(-3.99)
floor(-3.99) == -4.0
floor(5)
floor(5) == 5

// ceil() function tests
ceil(3.14)
ceil(3.14) == 4.0
ceil(3.01)
ceil(3.01) == 4.0
ceil(-3.14)
ceil(-3.14) == -3.0
ceil(-3.99)
ceil(-3.99) == -3.0
ceil(5)
ceil(5) == 5

// min() function tests
min(5, 3)
min(5, 3) == 3
min(3, 5)
min(3, 5) == 3
min(-5, -3)
min(-5, -3) == -5
min(3.14, 2.71)
min(3.14, 2.71) == 2.71
min(5, 3.14)
min(5, 3.14) == 3.14

// max() function tests
max(5, 3)
max(5, 3) == 5
max(3, 5)
max(3, 5) == 5
max(-5, -3)
max(-5, -3) == -3
max(3.14, 2.71)
max(3.14, 2.71) == 3.14
max(5, 3.14)
max(5, 3.14) == 5.0

// sum() function tests - arrays
sum([1, 2, 3, 4, 5])
sum([1, 2, 3, 4, 5]) == 15
sum([1.1, 2.2, 3.3])
sum([1.1, 2.2, 3.3]) == 6.6
sum([])
sum([]) == 0
sum([-1, -2, -3])
sum([-1, -2, -3]) == -6

// sum() function tests - lists
sum((1, 2, 3, 4, 5))
sum((1, 2, 3, 4, 5)) == 15
sum((1.1, 2.2, 3.3))
sum((1.1, 2.2, 3.3)) == 6.6
// Empty list test would need special handling
// sum(()) == 0
sum((-1, -2, -3))
sum((-1, -2, -3)) == -6

// avg() function tests - arrays
avg([1, 2, 3, 4, 5])
avg([1, 2, 3, 4, 5]) == 3.0
avg([2, 4, 6])
avg([2, 4, 6]) == 4.0
avg([1.5, 2.5, 3.5])
avg([1.5, 2.5, 3.5]) == 2.5
avg([-2, 0, 2])
avg([-2, 0, 2]) == 0.0

// avg() function tests - lists
avg((1, 2, 3, 4, 5))
avg((1, 2, 3, 4, 5)) == 3.0
avg((2, 4, 6))
avg((2, 4, 6)) == 4.0
avg((1.5, 2.5, 3.5))
avg((1.5, 2.5, 3.5)) == 2.5
avg((-2, 0, 2))
avg((-2, 0, 2)) == 0.0

// Mixed integer and float operations - arrays
min(5, 3.14)
min(5, 3.14) == 3.14
max(5, 3.14)
max(5, 3.14) == 5.0
sum([1, 2.5, 3])
sum([1, 2.5, 3]) == 6.5
avg([1, 2.0, 3])
avg([1, 2.0, 3]) == 2.0

// Mixed integer and float operations - lists
sum((1, 2.5, 3))
sum((1, 2.5, 3)) == 6.5
avg((1, 2.0, 3))
avg((1, 2.0, 3)) == 2.0

// Edge cases - arrays
abs(-0)
abs(-0) == 0
min(0, 0)
min(0, 0) == 0
max(0, 0)
max(0, 0) == 0
sum([0])
sum([0]) == 0
avg([5])
avg([5]) == 5.0

// Edge cases - lists
sum((0))
sum((0)) == 0
avg((5))
avg((5)) == 5.0

// Complex expressions - arrays and lists
abs(min(-5, -3))
abs(min(-5, -3)) == 5
max(abs(-3), abs(-7))
max(abs(-3), abs(-7)) == 7
sum([abs(-1), abs(-2), abs(-3)])
sum([abs(-1), abs(-2), abs(-3)]) == 6
avg([round(3.14), floor(3.99), ceil(2.01)])
avg([round(3.14), floor(3.99), ceil(2.01)]) == 3.0
sum((abs(-1), abs(-2), abs(-3)))
sum((abs(-1), abs(-2), abs(-3))) == 6
avg((round(3.14), floor(3.99), ceil(2.01)))
avg((round(3.14), floor(3.99), ceil(2.01))) == 3.0
