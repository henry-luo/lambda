# test_py_stdlib_math.py — Phase C: math module

import math

# constants
print(round(math.pi, 4))
print(round(math.e, 4))
print(round(math.tau, 4))

# basic math reused from Lambda
print(math.sqrt(16))
print(math.floor(3.7))
print(math.ceil(3.2))
print(math.trunc(3.9))

# trig
print(round(math.sin(0), 1))
print(round(math.cos(0), 1))

# pow/log
print(math.pow(2, 10))
print(round(math.log(math.e), 1))
print(round(math.log2(8), 1))
print(round(math.log10(1000), 1))

# new functions
print(math.factorial(5))
print(math.factorial(0))
print(math.gcd(12, 8))
print(math.lcm(4, 6))

# predicates
print(math.isnan(math.nan))
print(math.isinf(math.inf))
print(math.isfinite(42))
print(math.isfinite(math.inf))

# fabs, copysign, fmod
print(math.fabs(-3.5))
print(math.copysign(1, -5))
print(math.fmod(10, 3))

# degrees/radians
print(round(math.degrees(math.pi), 1))
print(round(math.radians(180), 4))
