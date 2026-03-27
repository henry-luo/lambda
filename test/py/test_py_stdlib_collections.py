# test_py_stdlib_collections.py — Phase C: collections module

from collections import Counter, OrderedDict

# Counter from list
c = Counter(["a", "b", "a", "c", "a", "b"])
print(c["a"])
print(c["b"])
print(c["c"])

# OrderedDict is just a dict
od = OrderedDict()
od["x"] = 1
print(od["x"])

# Counter from string
c2 = Counter("abracadabra")
print(c2["a"])
