// JetStream Benchmark: crypto-md5 (SunSpider)
// MD5 hash implementation (RFC 1321)
// Original: Paul Johnston 1999-2002
// Tests bitwise operations, array manipulation, and string processing

let CHRSZ = 8
let MASK32 = 0xFFFFFFFF

// Safe 32-bit addition
pn safe_add(x: int, y: int) {
    var ux: u32 = x
    var uy: u32 = y
    return int(ux + uy)
}

// Rotate left (32-bit)
pn bit_rol(num: int, cnt: int) {
    var n: u32 = num
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}

// MD5 helper functions
pn md5_cmn(q: int, a: int, b: int, x: int, s: int, t: int) {
    return safe_add(bit_rol(safe_add(safe_add(a, q), safe_add(x, t)), s), b)
}

pn md5_ff(a: int, b: int, c: int, d: int, x: int, s: int, t: int) {
    return md5_cmn(bor(band(b, c), band(band(bnot(b), MASK32), d)), a, b, x, s, t)
}

pn md5_gg(a: int, b: int, c: int, d: int, x: int, s: int, t: int) {
    return md5_cmn(bor(band(b, d), band(c, band(bnot(d), MASK32))), a, b, x, s, t)
}

pn md5_hh(a: int, b: int, c: int, d: int, x: int, s: int, t: int) {
    return md5_cmn(bxor(bxor(b, c), d), a, b, x, s, t)
}

pn md5_ii(a: int, b: int, c: int, d: int, x: int, s: int, t: int) {
    return md5_cmn(bxor(c, bor(b, band(bnot(d), MASK32))), a, b, x, s, t)
}

// Convert string to array of little-endian words
pn str2binl(str) {
    var slen = len(str)
    var bin_len = shr(slen * CHRSZ, 5) + 1
    var bin = fill(bin_len + 1, 0)
    var mask = shl(1, CHRSZ) - 1
    var i: int = 0
    while (i < slen * CHRSZ) {
        var char_idx: int = i / CHRSZ
        var ch = ord(str[char_idx])
        var word_idx = shr(i, 5)
        var bit_pos = i % 32
        bin[word_idx] = bor(bin[word_idx], shl(band(ch, mask), bit_pos))
        i = i + CHRSZ
    }
    return bin
}

// Convert little-endian words to hex string
pn binl2hex(binarray) {
    let hex_tab = "0123456789abcdef"
    var result = ""
    var i: int = 0
    while (i < len(binarray) * 4) {
        var word_idx = shr(i, 2)
        var byte_shift = (i % 4) * 8
        var hi = band(shr(binarray[word_idx], byte_shift + 4), 0xF)
        var lo = band(shr(binarray[word_idx], byte_shift), 0xF)
        result = result ++ slice(hex_tab, hi, hi + 1) ++ slice(hex_tab, lo, lo + 1)
        i = i + 1
    }
    return result
}

// Core MD5 computation
pn core_md5(x_in, input_len: int) {
    // Copy and pad
    var x_len = len(x_in)
    // Compute total needed: padding index + 14 + 1
    var pad_word = shr(input_len, 5)
    // ushr for the >>> in JS:  ((len + 64) >>> 9) << 4 + 14
    var block_idx = shl(shr(input_len + 64, 9), 4) + 14
    var total_len = block_idx + 2
    if (total_len < x_len + 10) {
        total_len = x_len + 10
    }
    var x = fill(total_len, 0)
    var ci: int = 0
    while (ci < x_len) {
        x[ci] = x_in[ci]
        ci = ci + 1
    }
    // Append padding
    x[pad_word] = bor(x[pad_word], shl(0x80, input_len % 32))
    // Append length
    x[block_idx] = input_len

    var a: int = 0x67452301
    var b: int = 0xEFCDAB89
    var c: int = 0x98BADCFE
    var d: int = 0x10325476

    var i: int = 0
    while (i < block_idx + 1) {
        var olda = a
        var oldb = b
        var oldc = c
        var oldd = d

        // Use unsigned 32-bit constants (negatives converted from JS)
        a = md5_ff(a, b, c, d, x[i+ 0], 7 , 0xD76AA478)
        d = md5_ff(d, a, b, c, x[i+ 1], 12, 0xE8C7B756)
        c = md5_ff(c, d, a, b, x[i+ 2], 17, 0x242070DB)
        b = md5_ff(b, c, d, a, x[i+ 3], 22, 0xC1BDCEEE)
        a = md5_ff(a, b, c, d, x[i+ 4], 7 , 0xF57C0FAF)
        d = md5_ff(d, a, b, c, x[i+ 5], 12, 0x4787C62A)
        c = md5_ff(c, d, a, b, x[i+ 6], 17, 0xA8304613)
        b = md5_ff(b, c, d, a, x[i+ 7], 22, 0xFD469501)
        a = md5_ff(a, b, c, d, x[i+ 8], 7 , 0x698098D8)
        d = md5_ff(d, a, b, c, x[i+ 9], 12, 0x8B44F7AF)
        c = md5_ff(c, d, a, b, x[i+10], 17, 0xFFFF5BB1)
        b = md5_ff(b, c, d, a, x[i+11], 22, 0x895CD7BE)
        a = md5_ff(a, b, c, d, x[i+12], 7 , 0x6B901122)
        d = md5_ff(d, a, b, c, x[i+13], 12, 0xFD987193)
        c = md5_ff(c, d, a, b, x[i+14], 17, 0xA679438E)
        b = md5_ff(b, c, d, a, x[i+15], 22, 0x49B40821)

        a = md5_gg(a, b, c, d, x[i+ 1], 5 , 0xF61E2562)
        d = md5_gg(d, a, b, c, x[i+ 6], 9 , 0xC040B340)
        c = md5_gg(c, d, a, b, x[i+11], 14, 0x265E5A51)
        b = md5_gg(b, c, d, a, x[i+ 0], 20, 0xE9B6C7AA)
        a = md5_gg(a, b, c, d, x[i+ 5], 5 , 0xD62F105D)
        d = md5_gg(d, a, b, c, x[i+10], 9 , 0x02441453)
        c = md5_gg(c, d, a, b, x[i+15], 14, 0xD8A1E681)
        b = md5_gg(b, c, d, a, x[i+ 4], 20, 0xE7D3FBC8)
        a = md5_gg(a, b, c, d, x[i+ 9], 5 , 0x21E1CDE6)
        d = md5_gg(d, a, b, c, x[i+14], 9 , 0xC33707D6)
        c = md5_gg(c, d, a, b, x[i+ 3], 14, 0xF4D50D87)
        b = md5_gg(b, c, d, a, x[i+ 8], 20, 0x455A14ED)
        a = md5_gg(a, b, c, d, x[i+13], 5 , 0xA9E3E905)
        d = md5_gg(d, a, b, c, x[i+ 2], 9 , 0xFCEFA3F8)
        c = md5_gg(c, d, a, b, x[i+ 7], 14, 0x676F02D9)
        b = md5_gg(b, c, d, a, x[i+12], 20, 0x8D2A4C8A)

        a = md5_hh(a, b, c, d, x[i+ 5], 4 , 0xFFFA3942)
        d = md5_hh(d, a, b, c, x[i+ 8], 11, 0x8771F681)
        c = md5_hh(c, d, a, b, x[i+11], 16, 0x6D9D6122)
        b = md5_hh(b, c, d, a, x[i+14], 23, 0xFDE5380C)
        a = md5_hh(a, b, c, d, x[i+ 1], 4 , 0xA4BEEA44)
        d = md5_hh(d, a, b, c, x[i+ 4], 11, 0x4BDECFA9)
        c = md5_hh(c, d, a, b, x[i+ 7], 16, 0xF6BB4B60)
        b = md5_hh(b, c, d, a, x[i+10], 23, 0xBEBFBC70)
        a = md5_hh(a, b, c, d, x[i+13], 4 , 0x289B7EC6)
        d = md5_hh(d, a, b, c, x[i+ 0], 11, 0xEAA127FA)
        c = md5_hh(c, d, a, b, x[i+ 3], 16, 0xD4EF3085)
        b = md5_hh(b, c, d, a, x[i+ 6], 23, 0x04881D05)
        a = md5_hh(a, b, c, d, x[i+ 9], 4 , 0xD9D4D039)
        d = md5_hh(d, a, b, c, x[i+12], 11, 0xE6DB99E5)
        c = md5_hh(c, d, a, b, x[i+15], 16, 0x1FA27CF8)
        b = md5_hh(b, c, d, a, x[i+ 2], 23, 0xC4AC5665)

        a = md5_ii(a, b, c, d, x[i+ 0], 6 , 0xF4292244)
        d = md5_ii(d, a, b, c, x[i+ 7], 10, 0x432AFF97)
        c = md5_ii(c, d, a, b, x[i+14], 15, 0xAB9423A7)
        b = md5_ii(b, c, d, a, x[i+ 5], 21, 0xFC93A039)
        a = md5_ii(a, b, c, d, x[i+12], 6 , 0x655B59C3)
        d = md5_ii(d, a, b, c, x[i+ 3], 10, 0x8F0CCC92)
        c = md5_ii(c, d, a, b, x[i+10], 15, 0xFFEFF47D)
        b = md5_ii(b, c, d, a, x[i+ 1], 21, 0x85845DD1)
        a = md5_ii(a, b, c, d, x[i+ 8], 6 , 0x6FA87E4F)
        d = md5_ii(d, a, b, c, x[i+15], 10, 0xFE2CE6E0)
        c = md5_ii(c, d, a, b, x[i+ 6], 15, 0xA3014314)
        b = md5_ii(b, c, d, a, x[i+13], 21, 0x4E0811A1)
        a = md5_ii(a, b, c, d, x[i+ 4], 6 , 0xF7537E82)
        d = md5_ii(d, a, b, c, x[i+11], 10, 0xBD3AF235)
        c = md5_ii(c, d, a, b, x[i+ 2], 15, 0x2AD7D2BB)
        b = md5_ii(b, c, d, a, x[i+ 9], 21, 0xEB86D391)

        a = safe_add(a, olda)
        b = safe_add(b, oldb)
        c = safe_add(c, oldc)
        d = safe_add(d, oldd)

        i = i + 16
    }
    return [a, b, c, d]
}

pn hex_md5(s) {
    var words = str2binl(s)
    var slen: int = len(s) * CHRSZ
    var hash = core_md5(words, slen)
    return binl2hex(hash)
}

pn run() {
    var plain_text = "Rebellious subjects, enemies to peace,\nProfaners of this neighbour-stained steel,--\nWill they not hear? What, ho! you men, you beasts,\nThat quench the fire of your pernicious rage\nWith purple fountains issuing from your veins,\nOn pain of torture, from those bloody hands\nThrow your mistemper'd weapons to the ground,\nAnd hear the sentence of your moved prince.\nThree civil brawls, bred of an airy word,\nBy thee, old Capulet, and Montague,\nHave thrice disturb'd the quiet of our streets,\nAnd made Verona's ancient citizens\nCast by their grave beseeming ornaments,\nTo wield old partisans, in hands as old,\nCanker'd with peace, to part your canker'd hate:\nIf ever you disturb our streets again,\nYour lives shall pay the forfeit of the peace.\nFor this time, all the rest depart away:\nYou Capulet; shall go along with me:\nAnd, Montague, come you this afternoon,\nTo know our further pleasure in this case,\nTo old Free-town, our common judgment-place.\nOnce more, on pain of death, all men depart."

    var i: int = 0
    while (i < 4) {
        plain_text = plain_text ++ plain_text
        i = i + 1
    }

    var md5_output = hex_md5(plain_text)
    var expected = "a831e91e0f70eddcb70dc61c6f82f6cd"
    if (md5_output != expected) {
        print("crypto-md5: FAIL got=" ++ md5_output ++ " expected=" ++ expected ++ "\n")
        return false
    }
    return true
}

pn main() {
    var __t0 = clock()
    // JetStream runs 22 iterations
    var pass = true
    var iter: int = 0
    while (iter < 22) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("crypto-md5: PASS\n")
    } else {
        print("crypto-md5: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
