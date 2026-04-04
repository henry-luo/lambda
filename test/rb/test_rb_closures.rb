# Test: Closure variable capture

# Test 1: lambda captures outer variable
x = 10
f = lambda { |y| x + y }
puts f.call(5)

# Test 2: proc captures outer variable
name = "world"
g = proc { "hello " + name }
puts g.call

# Test 3: stabby lambda captures outer variable
factor = 3
triple = -> (n) { n * factor }
puts triple.call(7)

# Test 4: closure captures multiple outer variables in expression
a = 5
b = 3
calc = lambda { |x| x + a - b }
puts calc.call(10)

# Test 5: multiple captures
a = 100
b = 200
sum_fn = lambda { a + b }
puts sum_fn.call
