# Test Tail Call Optimization (TCO) in Python transpiler
# Tail-recursive functions should run without stack overflow

# ==============================================================================
# PART 1: Basic tail-recursive functions
# ==============================================================================

# Factorial with accumulator (tail-recursive)
def factorial(n, acc):
    if n <= 1:
        return acc
    return factorial(n - 1, acc * n)

print(factorial(10, 1))
print(factorial(15, 1))

# Sum with accumulator (tail-recursive)
def sum_to(n, acc):
    if n <= 0:
        return acc
    return sum_to(n - 1, acc + n)

print(sum_to(100, 0))
print(sum_to(1000, 0))

# GCD — tail recursion in both branches
def gcd(a, b):
    if b == 0:
        return a
    return gcd(b, a % b)

print(gcd(48, 18))
print(gcd(1071, 462))

# Countdown (simple tail recursion)
def countdown(n):
    if n <= 0:
        return 0
    return countdown(n - 1)

print(countdown(100))

# ==============================================================================
# PART 2: if/elif/else with tail calls in multiple branches
# ==============================================================================

def classify(n, pos, neg, zero):
    if n > 0:
        return classify(n - 1, pos + 1, neg, zero)
    elif n < 0:
        return classify(n + 1, pos, neg + 1, zero)
    else:
        return [pos, neg, zero]

result = classify(5, 0, 0, 0)
print(result)

# ==============================================================================
# PART 3: Non-tail return BEFORE tail return (regression for block_returned bug)
# ==============================================================================

# Non-TCO return comes before TCO return in the same function body.
# This pattern exercises the block_returned reset fix.
def search(n, target):
    if n == target:
        return n * 10       # non-tail return (base case)
    if n > target + 100:
        return -1            # non-tail return (guard)
    return search(n + 1, target)  # tail call

print(search(0, 5))
print(search(0, 0))
print(search(90, 95))

# ==============================================================================
# PART 4: Deep recursion (only works with TCO)
# ==============================================================================

print(sum_to(100000, 0))
print(countdown(100000))
print(gcd(1000000, 999999))

# ==============================================================================
# PART 5: Takeuchi function — last return is tail, inner 3 are not
# ==============================================================================

def tak(x, y, z):
    if y >= x:
        return z
    a = tak(x - 1, y, z)
    b = tak(y - 1, z, x)
    c = tak(z - 1, x, y)
    return tak(a, b, c)

print(tak(18, 12, 6))

# ==============================================================================
# PART 6: Non-tail recursive (fib) — should NOT get TCO
# ==============================================================================

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

print(fib(10))
print(fib(20))
