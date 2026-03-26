# Test: Python class system — Phase 1
# Tests: class creation, instantiation, methods, inheritance, super(),
#        isinstance/issubclass, dunder methods, class/instance attributes

# ── Basic class + instance attributes ───────────────────────────────────────
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def to_str(self):
        return "(" + str(self.x) + ", " + str(self.y) + ")"

p = Point(3, 4)
print(p.x)
print(p.y)
print(p.to_str())

# ── Class attributes vs instance attributes ──────────────────────────────────
class Counter:
    count = 0
    def __init__(self, name):
        self.name = name
    def label(self):
        return self.name

c1 = Counter("first")
c2 = Counter("second")
print(c1.label())
print(c2.label())
print(c1.name)

# ── Single inheritance ────────────────────────────────────────────────────────
class Animal:
    def __init__(self, name):
        self.name = name
    def speak(self):
        return self.name + " makes a sound"
    def describe(self):
        return "Animal: " + self.name

class Dog(Animal):
    def speak(self):
        return self.name + " barks"

class Cat(Animal):
    def speak(self):
        return self.name + " meows"

d = Dog("Rex")
c = Cat("Whiskers")
print(d.speak())
print(c.speak())
print(d.describe())

# ── Multi-level inheritance ───────────────────────────────────────────────────
class GuideDog(Dog):
    def __init__(self, name, owner):
        super().__init__(name)
        self.owner = owner
    def speak(self):
        base = super().speak()
        return base + " (guide dog)"
    def info(self):
        return self.name + " guides " + self.owner

g = GuideDog("Buddy", "Alice")
print(g.speak())
print(g.info())
print(g.name)
print(g.owner)

# ── isinstance / issubclass ──────────────────────────────────────────────────
print(isinstance(d, Dog))
print(isinstance(d, Animal))
print(isinstance(d, Cat))
print(isinstance(g, GuideDog))
print(isinstance(g, Dog))
print(isinstance(g, Animal))

# ── Inherited __init__ (no own __init__ in subclass) ─────────────────────────
class ServiceDog(Dog):
    def speak(self):
        return self.name + " (service) barks"

s = ServiceDog("Max")
print(s.speak())
print(s.name)

# ── __str__ dunder ────────────────────────────────────────────────────────────
class Rectangle:
    def __init__(self, w, h):
        self.w = w
        self.h = h
    def area(self):
        return self.w * self.h
    def __str__(self):
        return "Rect(" + str(self.w) + "x" + str(self.h) + ")"

r = Rectangle(5, 3)
print(str(r))
print(r.area())

# ── __eq__ dunder ─────────────────────────────────────────────────────────────
class Vec2:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def __eq__(self, other):
        return self.x == other.x and self.y == other.y
    def __str__(self):
        return "Vec2(" + str(self.x) + "," + str(self.y) + ")"

v1 = Vec2(1, 2)
v2 = Vec2(1, 2)
v3 = Vec2(3, 4)
print(v1 == v2)
print(v1 == v3)
print(str(v1))

# ── super() in __init__ with grandparent ─────────────────────────────────────
class Vehicle:
    def __init__(self, brand):
        self.brand = brand
    def kind(self):
        return "vehicle"

class Car(Vehicle):
    def __init__(self, brand, model):
        super().__init__(brand)
        self.model = model
    def kind(self):
        return "car"

class ElectricCar(Car):
    def __init__(self, brand, model, range_km):
        super().__init__(brand, model)
        self.range_km = range_km
    def kind(self):
        return "electric car"
    def info(self):
        return self.brand + " " + self.model + " range=" + str(self.range_km)

ec = ElectricCar("Tesla", "Model3", 500)
print(ec.brand)
print(ec.model)
print(ec.range_km)
print(ec.kind())
print(ec.info())
print(isinstance(ec, ElectricCar))
print(isinstance(ec, Car))
print(isinstance(ec, Vehicle))
