#!/usr/bin/env python3
# Kostya Benchmark: base64 (Python)
# Base64 encode 10000 bytes, 100 iterations (manual implementation, no stdlib)
import time

TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"


def b64_encode(data):
    parts = []
    i = 0
    n = len(data)
    while i + 2 < n:
        b0, b1, b2 = data[i], data[i + 1], data[i + 2]
        parts.append(TABLE[b0 >> 2])
        parts.append(TABLE[((b0 & 3) << 4) | (b1 >> 4)])
        parts.append(TABLE[((b1 & 15) << 2) | (b2 >> 6)])
        parts.append(TABLE[b2 & 63])
        i += 3
    if i + 1 == n:
        b0 = data[i]
        parts.append(TABLE[b0 >> 2])
        parts.append(TABLE[(b0 & 3) << 4])
        parts.append("==")
    elif i + 2 == n:
        b0, b1 = data[i], data[i + 1]
        parts.append(TABLE[b0 >> 2])
        parts.append(TABLE[((b0 & 3) << 4) | (b1 >> 4)])
        parts.append(TABLE[(b1 & 15) << 2])
        parts.append("=")
    return ''.join(parts)


def b64_decode_len(encoded):
    slen = len(encoded)
    n = (slen >> 2) * 3
    if slen >= 1 and encoded[-1] == '=':
        n -= 1
    if slen >= 2 and encoded[-2] == '=':
        n -= 1
    return n


def main():
    num_bytes = 10000
    data = bytes([97] * num_bytes)  # 'a' * 10000

    t0 = time.perf_counter_ns()
    encoded = ""
    decoded_len = 0
    for _ in range(100):
        encoded = b64_encode(data)
        decoded_len = b64_decode_len(encoded)
    t1 = time.perf_counter_ns()

    enc_len = len(encoded)
    print(f"base64: encoded_len={enc_len} decoded_len={decoded_len}")
    if decoded_len == num_bytes:
        print("base64: PASS")
    else:
        print("base64: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
