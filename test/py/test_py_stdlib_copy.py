# test_py_stdlib_copy.py — Phase C: copy module

from copy import copy, deepcopy

# shallow copy of list
a = [1, 2, 3]
b = copy(a)
b.append(4)
print(a)
print(b)

# deep copy of nested list
x = [1, [2, 3]]
y = deepcopy(x)
y[1].append(4)
print(x)
print(y)
