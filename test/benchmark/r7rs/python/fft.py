#!/usr/bin/env python3
# R7RS Benchmark: fft (Python)
# Fast Fourier Transform - 1 iteration on 4096-element vector
import time
import math
import array

PI2 = 6.28318530717959


def four1(data, n):
    # bit-reversal
    i = 0
    j = 0
    while i < n:
        if i < j:
            data[i], data[j] = data[j], data[i]
            data[i + 1], data[j + 1] = data[j + 1], data[i + 1]
        m = n >> 1
        while m >= 2 and j >= m:
            j -= m
            m >>= 1
        j += m
        i += 2

    # Danielson-Lanczos
    mmax = 2
    while mmax < n:
        theta = PI2 / mmax
        sin_half = math.sin(0.5 * theta)
        wpr = -2.0 * sin_half * sin_half
        wpi = math.sin(theta)
        wr = 1.0
        wi = 0.0
        m2 = 0
        while m2 < mmax:
            ii = m2
            while ii < n:
                jj = ii + mmax
                tempr = wr * data[jj] - wi * data[jj + 1]
                tempi = wr * data[jj + 1] + wi * data[jj]
                data[jj] = data[ii] - tempr
                data[jj + 1] = data[ii + 1] - tempi
                data[ii] += tempr
                data[ii + 1] += tempi
                ii += mmax + mmax
            new_wr = wr * wpr - wi * wpi + wr
            wi = wi * wpr + wr * wpi + wi
            wr = new_wr
            m2 += 2
        mmax *= 2


def main():
    t0 = time.perf_counter_ns()
    data = array.array('d', [0.0] * 4096)
    four1(data, 4096)
    result = data[0]
    t1 = time.perf_counter_ns()

    if result == 0.0:
        print("fft: PASS")
    else:
        print(f"fft: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
