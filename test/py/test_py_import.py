# test_py_import.py — Phase E: single-file imports

# from ... import name1, name2
from utils import add, multiply, PI

print(add(2, 3))
print(multiply(4, 5))
print(PI)

# from ... import with alias
from utils import greet as say_hello
print(say_hello("World"))

# bare import: import module
import utils
print(utils.add(10, 20))
print(utils.VERSION)

# from ... import *
from utils import *
print(MAX_VALUE)
