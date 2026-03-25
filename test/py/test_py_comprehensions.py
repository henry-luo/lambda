# Test list comprehensions
squares = [x**2 for x in range(6)]
print(squares)

evens = [x for x in range(10) if x % 2 == 0]
print(evens)

doubled = [x * 2 for x in [1, 2, 3, 4, 5]]
print(doubled)

# Test dict comprehension
words = ["hello", "world", "python"]
lengths = {w: len(w) for w in words}
print(lengths)

# Test lambda
double = lambda x: x * 2
print(double(5))

add = lambda a, b: a + b
print(add(10, 20))

# Lambda in expressions
nums = [1, 2, 3, 4, 5]
result = list(map(lambda x: x ** 2, nums))
print(result)

# Lambda with conditional
sign = lambda x: "positive" if x > 0 else "negative"
print(sign(5))
print(sign(-3))
