# test_py_stdlib_functools.py — Phase C: functools module

from functools import reduce

# reduce
result = reduce(lambda x, y: x + y, [1, 2, 3, 4, 5])
print(result)

# reduce with initial
result2 = reduce(lambda x, y: x * y, [1, 2, 3, 4], 10)
print(result2)
