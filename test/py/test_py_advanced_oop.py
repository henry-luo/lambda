# Test: __init_subclass__, __class_getitem__, and metaclasses

# --- __init_subclass__ ---
class Plugin:
    _registry = []

    def __init_subclass__(cls, **kwargs):
        Plugin._registry.append(cls.__name__)

class AudioPlugin(Plugin):
    pass

class VideoPlugin(Plugin):
    pass

class NetworkPlugin(Plugin):
    pass

print(Plugin._registry)   # ['AudioPlugin', 'VideoPlugin', 'NetworkPlugin']

# --- __class_getitem__ ---
class TypedList:
    def __class_getitem__(cls, item_type):
        return "TypedList[" + item_type.__name__ + "]"

print(TypedList[int])    # TypedList[int]
print(TypedList[str])    # TypedList[str]

# --- Metaclass ---
class Meta(type):
    def __new__(mcs, name, bases, namespace):
        cls = type.__new__(mcs, name, bases, namespace)
        cls._created_by_meta = True
        return cls

class MyClass(metaclass=Meta):
    pass

print(hasattr(MyClass, "_created_by_meta"))   # True
print(MyClass._created_by_meta)               # True

# Metaclass that auto-registers subclasses
class RegistryMeta(type):
    _registry = {}

    def __new__(mcs, name, bases, namespace):
        cls = type.__new__(mcs, name, bases, namespace)
        if bases:  # skip the base class itself
            RegistryMeta._registry[name] = cls
        return cls

class Base(metaclass=RegistryMeta):
    pass

class Alpha(Base):
    pass

class Beta(Base):
    pass

print(sorted(RegistryMeta._registry.keys()))  # ['Alpha', 'Beta']
