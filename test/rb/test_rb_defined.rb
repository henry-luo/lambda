# Test: defined? keyword

# Test 1: defined variable
x = 42
puts defined?(x)

# Test 2: undefined variable
puts defined?(y).nil?

# Test 3: defined literal
puts defined?(42)

# Test 4: defined string
puts defined?("hello")
