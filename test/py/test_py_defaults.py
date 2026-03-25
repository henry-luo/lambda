# Test default parameters
def greet(name, greeting="Hello"):
    return greeting + ", " + name

print(greet("World"))
print(greet("World", "Hi"))

# Test keyword arguments
print(greet(greeting="Hey", name="Lambda"))

# Multiple defaults
def make_list(x, y=10, z=20):
    return [x, y, z]

print(make_list(1))
print(make_list(1, 2))
print(make_list(1, 2, 3))
print(make_list(1, z=30))

# Function with no defaults (regression)
def add(a, b):
    return a + b

print(add(3, 4))

# print kwargs
print(1, 2, 3, sep="-")
print("hello", end="!\n")
print("a", "b", "c", sep=", ", end=".\n")
