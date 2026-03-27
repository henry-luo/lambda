# test_py_stdlib_random.py — Phase C: random module

import random

# seed for reproducible results
random.seed(42)

# random.random() returns float in [0, 1)
r = random.random()
print(0 <= r < 1)

# random.randint(a, b)
ri = random.randint(1, 10)
print(1 <= ri <= 10)

# random.choice
items = [10, 20, 30, 40, 50]
c = random.choice(items)
print(c in items)
