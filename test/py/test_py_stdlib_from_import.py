# test_py_stdlib_from_import.py — Phase C: from...import pattern

from math import sqrt, pi, factorial
from os.path import join, basename
from functools import reduce
from json import loads, dumps

# math
print(sqrt(25))
print(round(pi, 4))
print(factorial(6))

# os.path
print(join("a", "b"))
print(basename("/path/to/file.txt"))

# functools
print(reduce(lambda a, b: a + b, [1, 2, 3, 4, 5]))

# json
d = loads('{"key": "value"}')
print(d["key"])
