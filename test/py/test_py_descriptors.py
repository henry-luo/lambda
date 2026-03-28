# Test: Full descriptor protocol (__get__, __set__, __delete__)

# --- Non-data descriptor (read-only, __get__ only) ---
class UpperDescriptor:
    """Descriptor that returns the stored string in upper case."""
    def __init__(self, attr):
        self.attr = attr

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        val = getattr(obj, self.attr)
        if val is None:
            return None
        return val.upper()

class Person:
    name = UpperDescriptor("_name")

    def __init__(self, name):
        self._name = name

p = Person("alice")
print(p.name)    # ALICE
p._name = "bob"
print(p.name)    # BOB

# --- Data descriptor (__get__ + __set__) ---
class RangeDescriptor:
    """Descriptor that enforces a valid integer range."""
    def __init__(self, attr, lo, hi):
        self.attr = attr
        self.lo = lo
        self.hi = hi

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return getattr(obj, self.attr)

    def __set__(self, obj, value):
        if value < self.lo or value > self.hi:
            raise ValueError("out of range")
        setattr(obj, self.attr, value)

class Gauge:
    level = RangeDescriptor("_level", 0, 100)

    def __init__(self, level):
        self.level = level

g = Gauge(50)
print(g.level)     # 50
g.level = 75
print(g.level)     # 75
try:
    g.level = 110
    print("should not reach here")
except ValueError as e:
    print("caught:", e)

# --- Data descriptor with __delete__ ---
class DeleteTracker:
    def __init__(self, attr):
        self.attr = attr

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return getattr(obj, self.attr)

    def __set__(self, obj, value):
        setattr(obj, self.attr, value)

    def __delete__(self, obj):
        print("deleting", self.attr)

class Tracked:
    x = DeleteTracker("_x")

    def __init__(self, v):
        self.x = v

t = Tracked(99)
print(t.x)     # 99
del t.x        # deleting _x
