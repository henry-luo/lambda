# Test: Performance - native arithmetic, comparisons, and loop optimization

# 1. Integer arithmetic in a loop (sum 0..999)
sum = 0
for i in 0...1000
  sum += i
end
puts sum

# 2. Nested arithmetic
a = 10
b = 20
c = 30
result = a * b + c * 2 - a / 2
puts result

# 3. Float arithmetic
x = 3.14
y = 2.0
z = x * y + 1.0
puts z

# 4. Comparison-heavy loop (count evens)
count = 0
for i in 0...100
  if i % 2 == 0
    count += 1
  end
end
puts count

# 5. Fibonacci with native arithmetic
def fib(n)
  a = 0
  b = 1
  i = 0
  while i < n
    temp = a + b
    a = b
    b = temp
    i += 1
  end
  a
end
puts fib(10)
puts fib(20)

# 6. Inclusive range loop
total = 0
for i in 1..10
  total += i
end
puts total

# 7. Nested loops with arithmetic
sum2 = 0
for i in 0...10
  for j in 0...10
    sum2 += i * j
  end
end
puts sum2

# 8. Mixed operations
val = 100
for i in 1..5
  val = val + i * 2 - 1
end
puts val

# 9. Comparison chain
a = 5
b = 10
c = 15
puts a < b
puts b < c
puts c > a
puts a == 5
puts b != c
