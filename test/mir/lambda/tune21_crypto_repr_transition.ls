// T21-5 (D8.6.2): typed crypto_sha12's core_sha1 failed to compile on the
// 176bdd934..33a178ed0 builds with "mir-value: unavailable representation
// transition 1 -> 2 in _core_sha1" (Item -> IntLane) and passes on HEAD. This
// fixture keeps that whole typed lowering under the emission gate so the
// missing conversion pair cannot regress silently; the golden pins the digests.
// JetStream Benchmark: crypto-sha1 (SunSpider) (Typed version)
// SHA-1 hash implementation
// Original: Paul Johnston 2000-2002 (FIPS PUB 180-1)
// Tests bitwise operations, array manipulation, and string processing

let CHRSZ = 8  // bits per input character (ASCII)
let MASK32 = 0xFFFFFFFF

// 32-bit addition relies on u32 wraparound. Unsigned sized contracts reject
// negative signed inputs, so normalize each signed 32-bit word before the
// explicit conversion instead of relying on an invalid negative-to-u32 cast.
pn safe_add(x: int, y: int) int {
    var ux: u32 = if (x < 0) { x + 4294967296 } else { x }
    var uy: u32 = if (y < 0) { y + 4294967296 } else { y }
    return int(ux + uy)
}

// Rotate left (32-bit)
pn rol(num: int, cnt: int) int {
    var n: u32 = num
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}

// SHA-1 round function
pn sha1_ft(t: int, b: int, c: int, d: int) int {
    if (t < 20) {
        return bor(band(b, c), band(band(bnot(b), MASK32), d))
    }
    if (t < 40) {
        return bxor(bxor(b, c), d)
    }
    if (t < 60) {
        return bor(bor(band(b, c), band(b, d)), band(c, d))
    }
    return bxor(bxor(b, c), d)
}

// SHA-1 round constant
pn sha1_kt(t: int) int {
    if (t < 20) {
        return 0x5A827999
    }
    if (t < 40) {
        return 0x6ED9EBA1
    }
    if (t < 60) {
        return 0x8F1BBCDC
    }
    return 0xCA62C1D6
}

// Convert string to array of big-endian words
pn str2binb(str: string) int[] {
    var slen: int = len(str)
    var bin_len: int = shr(slen * CHRSZ, 5) + 1
    var bin: int[] = fill(bin_len + 1, 0)
    var mask: int = shl(1, CHRSZ) - 1
    var i: int = 0
    while (i < slen * CHRSZ) {
        var char_idx: int = i / CHRSZ
        var ch: int = ord(str[char_idx])
        var word_idx: int = shr(i, 5)
        var bit_pos: int = 32 - CHRSZ - (i % 32)
        bin[word_idx] = bor(bin[word_idx], shl(band(ch, mask), bit_pos))
        i = i + CHRSZ
    }
    return bin
}

// Core SHA-1 computation on array of big-endian words
pn core_sha1(x_in: int[], input_len: int) int[] {
    // Copy input to mutable array with padding space
    var x_len: int = len(x_in)
    var padded_len: int = shr(input_len + 64, 9)
    var total_len: int = shl(padded_len, 4) + 16 + 1
    if (total_len < x_len + 20) {
        total_len = x_len + 20
    }
    var x: int[] = fill(total_len, 0)
    var ci: int = 0
    while (ci < x_len) {
        x[ci] = x_in[ci]
        ci = ci + 1
    }
    // Append padding bit
    var pad_idx: int = shr(input_len, 5)
    x[pad_idx] = bor(x[pad_idx], shl(0x80, 24 - (input_len % 32)))
    // Append length
    var len_idx: int = shl(padded_len, 4) + 15
    x[len_idx] = input_len

    var w: int[] = fill(80, 0)
    var a: int = 0x67452301
    var b: int = 0xEFCDAB89
    var c: int = 0x98BADCFE
    var d: int = 0x10325476
    var e: int = 0xC3D2E1F0

    var i: int = 0
    while (i < len_idx + 1) {
        var olda: int = a
        var oldb: int = b
        var oldc: int = c
        var oldd: int = d
        var olde: int = e

        var j: int = 0
        while (j < 80) {
            if (j < 16) {
                w[j] = x[i + j]
            } else {
                w[j] = rol(bxor(bxor(w[j - 3], w[j - 8]), bxor(w[j - 14], w[j - 16])), 1)
            }
            // An arithmetic error becomes a mismatching digest rather than entering an int local.
            var t_result = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)),
                             safe_add(safe_add(e, w[j]), sha1_kt(j)))
            var t: int = match t_result { case error: 0 case int: t_result }
            e = d
            d = c
            c = rol(b, 30)
            b = a
            a = t
            j = j + 1
        }

        a = safe_add(a, olda)
        b = safe_add(b, oldb)
        c = safe_add(c, oldc)
        d = safe_add(d, oldd)
        e = safe_add(e, olde)

        i = i + 16
    }
    return [a, b, c, d, e]
}

// Convert array of big-endian words to hex string
pn binb2hex(binarray: int[]) string {
    let hex_chars = "0123456789abcdef"
    var result: string = ""
    var i: int = 0
    while (i < len(binarray) * 4) {
        var word_idx: int = shr(i, 2)
        var byte_shift: int = (3 - (i % 4)) * 8
        var hi: int = band(shr(binarray[word_idx], byte_shift + 4), 0xF)
        var lo: int = band(shr(binarray[word_idx], byte_shift), 0xF)
        result = result ++ slice(hex_chars, hi, hi + 1) ++ slice(hex_chars, lo, lo + 1)
        i = i + 1
    }
    return result
}

// Compute hex SHA-1 of a string
pn hex_sha1(s: string) string {
    var words = str2binb(s)
    var hash = core_sha1(words, len(s) * CHRSZ)
    return binb2hex(hash)
}

pn main() {
    print(hex_sha1("abc")); print("\n")
    print(hex_sha1("")); print("\n")
    print(hex_sha1("The quick brown fox jumps over the lazy dog")); print("\n")
}
