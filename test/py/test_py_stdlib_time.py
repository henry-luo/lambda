# test_py_stdlib_time.py — Phase C: time module

import time

# time.time returns a float > 0
t = time.time()
print(t > 0)

# time.monotonic
m = time.monotonic()
print(m > 0)

# time.perf_counter
p = time.perf_counter()
print(p > 0)
