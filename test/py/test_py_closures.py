# Test read-only closures
def make_adder(n):
    return lambda x: x + n

add5 = make_adder(5)
print(add5(10))
print(add5(20))

add3 = make_adder(3)
print(add3(7))

# Test nested function closure (read-only)
def make_greeter(greeting):
    def greet(name):
        return greeting + " " + name
    return greet

hello = make_greeter("Hello")
print(hello("World"))
print(hello("Lambda"))

# Test nonlocal (mutable closure)
def make_counter():
    count = 0
    def increment():
        nonlocal count
        count = count + 1
        return count
    return increment

c = make_counter()
print(c())
print(c())
print(c())
