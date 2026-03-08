// JetStream Benchmark: crypto-sha1 (SunSpider)
// SHA-1 hash implementation
// Original: Paul Johnston 2000-2002 (FIPS PUB 180-1)
// Tests bitwise operations, array manipulation, and string processing

let CHRSZ = 8  // bits per input character (ASCII)

// Safe 32-bit addition with masking
pn safe_add(x: int, y: int) {
    return band(x + y, 4294967295)
}

// Rotate left (32-bit)
// Lambda uses 64-bit ints, so shl can overflow 32 bits. Must mask result.
pn rol(num: int, cnt: int) {
    var n = band(num, 4294967295)
    return band(bor(shl(n, cnt), shr(n, 32 - cnt)), 4294967295)
}

// SHA-1 round function
pn sha1_ft(t: int, b: int, c: int, d: int) {
    if (t < 20) {
        return bor(band(b, c), band(band(bnot(b), 4294967295), d))
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
pn sha1_kt(t: int) {
    if (t < 20) {
        return 1518500249
    }
    if (t < 40) {
        return 1859775393
    }
    if (t < 60) {
        return 2400959708  // 0x8F1BBCDC
    }
    return 3395469782  // 0xCA62C1D6
}

// Convert string to array of big-endian words
pn str2binb(str: string) {
    var slen = len(str)
    var bin_len = shr(slen * CHRSZ, 5) + 1
    var bin = fill(bin_len + 1, 0)
    var mask = shl(1, CHRSZ) - 1
    var i: int = 0
    while (i < slen * CHRSZ) {
        var char_idx: int = i / CHRSZ
        var ch = ord(slice(str, char_idx, char_idx + 1))
        var word_idx = shr(i, 5)
        var bit_pos = 32 - CHRSZ - (i % 32)
        bin[word_idx] = bor(bin[word_idx], shl(band(ch, mask), bit_pos))
        i = i + CHRSZ
    }
    return bin
}

// Core SHA-1 computation on array of big-endian words
pn core_sha1(x_in, input_len) {
    // Copy input to mutable array with padding space
    var x_len = len(x_in)
    var padded_len = shr(input_len + 64, 9)
    var total_len = shl(padded_len, 4) + 16 + 1
    if (total_len < x_len + 20) {
        total_len = x_len + 20
    }
    var x = fill(total_len, 0)
    var ci: int = 0
    while (ci < x_len) {
        x[ci] = x_in[ci]
        ci = ci + 1
    }
    // Append padding bit
    var pad_idx = shr(input_len, 5)
    x[pad_idx] = bor(x[pad_idx], shl(128, 24 - (input_len % 32)))
    // Append length
    var len_idx = shl(padded_len, 4) + 15
    x[len_idx] = input_len

    var w = fill(80, 0)
    var a = 1732584193   // 0x67452301
    var b = 4023233417   // 0xEFCDAB89
    var c = 2562383102   // 0x98BADCFE
    var d = 271733878    // 0x10325476
    var e = 3285377520   // 0xC3D2E1F0

    var i: int = 0
    while (i < len_idx + 1) {
        var olda = a
        var oldb = b
        var oldc = c
        var oldd = d
        var olde = e

        var j: int = 0
        while (j < 80) {
            if (j < 16) {
                w[j] = x[i + j]
            } else {
                w[j] = rol(bxor(bxor(w[j - 3], w[j - 8]), bxor(w[j - 14], w[j - 16])), 1)
            }
            var t = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)),
                             safe_add(safe_add(e, w[j]), sha1_kt(j)))
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
pn binb2hex(binarray) {
    let hex_chars = "0123456789abcdef"
    var result = ""
    var i: int = 0
    while (i < len(binarray) * 4) {
        var word_idx = shr(i, 2)
        var byte_shift = (3 - (i % 4)) * 8
        var hi = band(shr(binarray[word_idx], byte_shift + 4), 15)
        var lo = band(shr(binarray[word_idx], byte_shift), 15)
        result = result ++ slice(hex_chars, hi, hi + 1) ++ slice(hex_chars, lo, lo + 1)
        i = i + 1
    }
    return result
}

// Compute hex SHA-1 of a string
pn hex_sha1(s: string) {
    var words = str2binb(s)
    var hash = core_sha1(words, len(s) * CHRSZ)
    return binb2hex(hash)
}

pn run() {
    var plain_text = "Two households, both alike in dignity,\nIn fair Verona, where we lay our scene,\nFrom ancient grudge break to new mutiny,\nWhere civil blood makes civil hands unclean.\nFrom forth the fatal loins of these two foes\nA pair of star-cross'd lovers take their life;\nWhole misadventured piteous overthrows\nDo with their death bury their parents' strife.\nThe fearful passage of their death-mark'd love,\nAnd the continuance of their parents' rage,\nWhich, but their children's end, nought could remove,\nIs now the two hours' traffic of our stage;\nThe which if you with patient ears attend,\nWhat here shall miss, our toil shall strive to mend."

    // Double the text 4 times (like original: for i=0..3 plainText += plainText)
    var i: int = 0
    while (i < 4) {
        plain_text = plain_text ++ plain_text
        i = i + 1
    }

    var sha1_output = hex_sha1(plain_text)
    var expected = "2524d264def74cce2498bf112bedf00e6c0b796d"
    if (sha1_output != expected) {
        print("crypto-sha1: FAIL got=" ++ sha1_output ++ " expected=" ++ expected ++ "\n")
        return false
    }
    return true
}

pn main() {
    var __t0 = clock()
    // JetStream runs 25 iterations
    var pass = true
    var iter: int = 0
    while (iter < 25) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("crypto-sha1: PASS\n")
    } else {
        print("crypto-sha1: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
