#!/usr/bin/env python3
# Larceny Benchmark: deriv (Python)
# Symbolic differentiation of expression trees
import time

# Node types: 0=const, 1=var, 2=add, 3=mul


def make_const(v):
    return (0, v, None, None)


def make_var():
    return (1, 0, None, None)


def make_add(l, r):
    return (2, 0, l, r)


def make_mul(l, r):
    return (3, 0, l, r)


def deriv(e):
    t = e[0]
    if t == 0:
        return make_const(0)
    if t == 1:
        return make_const(1)
    if t == 2:
        return make_add(deriv(e[2]), deriv(e[3]))
    # t == 3: product rule d(a*b) = a*db + da*b
    dl = deriv(e[2])
    dr = deriv(e[3])
    return make_add(make_mul(e[2], dr), make_mul(dl, e[3]))


def count_nodes(e):
    t = e[0]
    if t == 0 or t == 1:
        return 1
    return 1 + count_nodes(e[2]) + count_nodes(e[3])


def make_expr():
    c3 = make_const(3)
    c2 = make_const(2)
    c5 = make_const(5)
    x = make_var
    m1 = make_mul(c3, x())
    m2 = make_mul(m1, x())
    m3 = make_mul(m2, x())
    m4 = make_mul(c2, x())
    m5 = make_mul(m4, x())
    a1 = make_add(m3, m5)
    a2 = make_add(a1, x())
    return make_add(a2, c5)


def benchmark():
    result = 0
    for _ in range(5000):
        e = make_expr()
        d = deriv(e)
        result = count_nodes(d)
    return result


def main():
    t0 = time.perf_counter_ns()
    result = benchmark()
    t1 = time.perf_counter_ns()

    if result == 45:
        print("deriv: PASS")
    else:
        print(f"deriv: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
