# test_py_packages.py — Phase E: Full package system tests

# Test 1: import package (loads __init__.py)
import mypkg
print(mypkg.greet("World"))
print(mypkg.PACKAGE_VERSION)

# Test 2: from package import name (greet re-exported from __init__.py)
from mypkg import greet
print(greet("Lambda"))

# Test 3: from package.module import function
from mypkg.utils import helper
print(helper())

# Test 4: from package import submodule (loads mypkg/math_helpers.py)
from mypkg import math_helpers
print(math_helpers.double(7))
print(math_helpers.square(5))

# Test 5: dotted import (import package.module)
import mypkg.utils
print(mypkg.utils.UTILS_CONST)

# Test 6: from package.sub import name (nested sub-package)
from mypkg.sub import deep
print(deep.deep_func())

# Test 7: from package.sub.deep import function
from mypkg.sub.deep import deep_func
print(deep_func())
