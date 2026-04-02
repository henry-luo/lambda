# Test: yield and methods with blocks

# Simple yield
def double_it(x, &block)
  yield x * 2
end

double_it(5) { |val| puts val }

# repeat with block
def repeat(n, &block)
  i = 0
  while i < n
    yield i
    i += 1
  end
end

repeat(3) { |i| puts "Step #{i}" }
