#!/usr/bin/env python3
"""JetStream Benchmark: crypto-sha1 (SunSpider) — Python version
SHA-1 hash implementation
Original: Paul Johnston 2000-2002 (FIPS PUB 180-1)
Tests bitwise operations, array manipulation, and string processing
"""
import time

CHRSZ = 8   # bits per input character (ASCII)
MASK32 = 0xFFFFFFFF


def safe_add(x, y):
    return (x + y) & MASK32


def rol(num, cnt):
    n = num & MASK32
    return ((n << cnt) | (n >> (32 - cnt))) & MASK32


def sha1_ft(t, b, c, d):
    if t < 20:
        return (b & c) | ((~b & MASK32) & d)
    if t < 40:
        return b ^ c ^ d
    if t < 60:
        return (b & c) | (b & d) | (c & d)
    return b ^ c ^ d


def sha1_kt(t):
    if t < 20:
        return 1518500249
    if t < 40:
        return 1859775393
    if t < 60:
        return 2400959708   # 0x8F1BBCDC
    return 3395469782       # 0xCA62C1D6


def str2binb(s):
    slen = len(s)
    bin_len = (slen * CHRSZ >> 5) + 1
    binarray = [0] * (bin_len + 1)
    mask = (1 << CHRSZ) - 1
    for i in range(0, slen * CHRSZ, CHRSZ):
        char_idx = i // CHRSZ
        ch = ord(s[char_idx])
        word_idx = i >> 5
        bit_pos = 32 - CHRSZ - (i % 32)
        binarray[word_idx] |= (ch & mask) << bit_pos
    return binarray


def core_sha1(x_in, input_len):
    x_len = len(x_in)
    padded_len = (input_len + 64) >> 9
    total_len = (padded_len << 4) + 16 + 1
    if total_len < x_len + 20:
        total_len = x_len + 20
    x = [0] * total_len
    for i in range(x_len):
        x[i] = x_in[i]
    # append padding bit
    pad_idx = input_len >> 5
    x[pad_idx] |= 128 << (24 - (input_len % 32))
    # append length
    len_idx = (padded_len << 4) + 15
    x[len_idx] = input_len

    w = [0] * 80
    a = 1732584193
    b = 4023233417
    c = 2562383102
    d = 271733878
    e = 3285377520

    i = 0
    while i <= len_idx:
        olda, oldb, oldc, oldd, olde = a, b, c, d, e
        for j in range(80):
            if j < 16:
                w[j] = x[i + j]
            else:
                w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1)
            t = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)),
                         safe_add(safe_add(e, w[j]), sha1_kt(j)))
            e = d
            d = c
            c = rol(b, 30)
            b = a
            a = t
        a = safe_add(a, olda)
        b = safe_add(b, oldb)
        c = safe_add(c, oldc)
        d = safe_add(d, oldd)
        e = safe_add(e, olde)
        i += 16

    return [a, b, c, d, e]


def binb2hex(binarray):
    hex_chars = "0123456789abcdef"
    result = []
    for i in range(len(binarray) * 4):
        word_idx = i >> 2
        byte_shift = (3 - (i % 4)) * 8
        hi = (binarray[word_idx] >> (byte_shift + 4)) & 15
        lo = (binarray[word_idx] >> byte_shift) & 15
        result.append(hex_chars[hi])
        result.append(hex_chars[lo])
    return ''.join(result)


def hex_sha1(s):
    words = str2binb(s)
    hash_arr = core_sha1(words, len(s) * CHRSZ)
    return binb2hex(hash_arr)


def run():
    plain_text = (
        "Two households, both alike in dignity,\n"
        "In fair Verona, where we lay our scene,\n"
        "From ancient grudge break to new mutiny,\n"
        "Where civil blood makes civil hands unclean.\n"
        "From forth the fatal loins of these two foes\n"
        "A pair of star-cross'd lovers take their life;\n"
        "Whole misadventured piteous overthrows\n"
        "Do with their death bury their parents' strife.\n"
        "The fearful passage of their death-mark'd love,\n"
        "And the continuance of their parents' rage,\n"
        "Which, but their children's end, nought could remove,\n"
        "Is now the two hours' traffic of our stage;\n"
        "The which if you with patient ears attend,\n"
        "What here shall miss, our toil shall strive to mend."
    )
    for _ in range(4):
        plain_text += plain_text

    sha1_output = hex_sha1(plain_text)
    expected = "2524d264def74cce2498bf112bedf00e6c0b796d"
    return sha1_output == expected


def main():
    t0 = time.perf_counter_ns()
    pass_all = True
    for _ in range(25):
        if not run():
            pass_all = False
    t1 = time.perf_counter_ns()

    if pass_all:
        print("crypto-sha1: PASS")
    else:
        print("crypto-sha1: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()
