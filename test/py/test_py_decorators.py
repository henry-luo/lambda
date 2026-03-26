# Phase C: Decorators — function decorators, stacked decorators,
# decorator factories, class decorators, and @property

# ---- basic function decorator (single-arg wrapper) ----
def bold(fn):
    def bold_wrapper(x):
        return "<b>" + fn(x) + "</b>"
    return bold_wrapper

@bold
def greet(name):
    return "Hello, " + name

print(greet("World"))


# ---- stacked decorators (applied bottom-to-top) ----
def italic(fn):
    def italic_wrapper(x):
        return "<i>" + fn(x) + "</i>"
    return italic_wrapper

@bold
@italic
def shout(text):
    return text.upper()

print(shout("hello"))


# ---- decorator factory (decorator with arguments) ----
def repeat(n):
    def make_repeated(fn):
        def repeated_wrapper(x):
            result = ""
            for i in range(n):
                if result:
                    result += " "
                result += fn(x)
            return result
        return repeated_wrapper
    return make_repeated

@repeat(3)
def say(word):
    return word

print(say("hi"))


# ---- class decorator ----
def add_repr(cls):
    cls.label = "tagged"
    return cls

@add_repr
class Point:
    pass

p = Point()
print(p.label)


# ---- @property ----
class Circle:
    def __init__(self, radius):
        self.radius = radius

    @property
    def area(self):
        return 3.14159 * self.radius * self.radius

c = Circle(5)
print(c.area)


# ---- identity decorator (smoke test) ----
def identity(fn):
    return fn

@identity
def add(a, b):
    return a + b

print(add(2, 3))
