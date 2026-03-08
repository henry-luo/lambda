#!/usr/bin/env python3
# Larceny Benchmark: paraffins (Python)
# Count paraffin isomers using radical enumeration with multiset coefficients
import time


def ms2(r):
    return (r * (r + 1)) >> 1


def ms3(r):
    return r * (r + 1) * (r + 2) // 6


def ms4(r):
    return r * (r + 1) * (r + 2) * (r + 3) // 24


def count_radicals(rcount, k):
    count = 0
    target = k - 1
    nc1 = 0
    while nc1 * 3 <= target:
        nc2 = nc1
        while nc1 + nc2 * 2 <= target:
            nc3 = target - nc1 - nc2
            if nc3 >= nc2:
                r1, r2, r3 = rcount[nc1], rcount[nc2], rcount[nc3]
                if nc1 == nc2 == nc3:
                    count += ms3(r1)
                elif nc1 == nc2:
                    count += ms2(r1) * r3
                elif nc2 == nc3:
                    count += r1 * ms2(r2)
                else:
                    count += r1 * r2 * r3
            nc2 += 1
        nc1 += 1
    return count


def count_bcp(rcount, n):
    if n % 2 != 0:
        return 0
    r = rcount[n >> 1]
    return ms2(r)


def count_ccp(rcount, n):
    m = n - 1
    max_rad = m >> 1
    count = 0
    nc1 = 0
    while nc1 * 4 <= m:
        nc2 = nc1
        while nc1 + nc2 * 3 <= m:
            remain = m - nc1 - nc2
            nc3 = nc2
            while nc3 * 2 <= remain:
                nc4 = remain - nc3
                if nc3 <= nc4 <= max_rad:
                    r1, r2, r3, r4 = rcount[nc1], rcount[nc2], rcount[nc3], rcount[nc4]
                    if nc1 == nc2 == nc3 == nc4:
                        count += ms4(r1)
                    elif nc1 == nc2 == nc3:
                        count += ms3(r1) * r4
                    elif nc2 == nc3 == nc4:
                        count += r1 * ms3(r2)
                    elif nc1 == nc2 and nc3 == nc4:
                        count += ms2(r1) * ms2(r3)
                    elif nc1 == nc2:
                        count += ms2(r1) * r3 * r4
                    elif nc2 == nc3:
                        count += r1 * ms2(r2) * r4
                    elif nc3 == nc4:
                        count += r1 * r2 * ms2(r3)
                    else:
                        count += r1 * r2 * r3 * r4
                nc3 += 1
            nc2 += 1
        nc1 += 1
    return count


def nb(n):
    if n < 1:
        return 0
    half = n >> 1
    rcount = [0] * (half + 1)
    rcount[0] = 1
    for k in range(1, half + 1):
        rcount[k] = count_radicals(rcount, k)
    return count_bcp(rcount, n) + count_ccp(rcount, n)


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(10):
        for n in range(1, 24):
            result = nb(n)
    t1 = time.perf_counter_ns()

    print(f"paraffins: nb(23) = {result}")
    if result == 5731580:
        print("paraffins: PASS")
    else:
        print(f"paraffins: FAIL (expected 5731580)")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
