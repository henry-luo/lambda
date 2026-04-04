# Test: Proc and Lambda support

# Test 1: lambda with .call
double = lambda { |x| x * 2 }
puts double.call(5)

# Test 2: stabby lambda with .call
square = -> (x) { x * x }
puts square.call(4)

# Test 3: Proc.new with .call
add = Proc.new { |a, b| a + b }
puts add.call(3, 7)

# Test 4: lambda with no args
greet = lambda { "hello from lambda" }
puts greet.call

# Test 5: proc with multiple statements
calc = proc { |x|
  y = x + 10
  y * 2
}
puts calc.call(5)
