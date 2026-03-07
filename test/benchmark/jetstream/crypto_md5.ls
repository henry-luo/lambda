// JetStream Benchmark: crypto-md5 (SunSpider)
// MD5 hash implementation (RFC 1321)
// Original: Paul Johnston 1999-2002
// Tests bitwise operations, array manipulation, and string processing

let CHRSZ = 8
let MASK32 = 4294967295

// Safe 32-bit addition
pn safe_add(x: int, y: int) {
    return band(band(x, MASK32) + band(y, MASK32), MASK32)
}

// Rotate left (32-bit) — Lambda uses 64-bit ints so must mask
pn bit_rol(num: int, cnt: int) {
    var n = band(num, MASK32)
    return band(bor(shl(n, cnt), shr(n, 32 - cnt)), MASK32)
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
        var ch = ord(slice(str, char_idx, char_idx + 1))
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
        var hi = band(shr(binarray[word_idx], byte_shift + 4), 15)
        var lo = band(shr(binarray[word_idx], byte_shift), 15)
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
    x[pad_word] = bor(x[pad_word], shl(128, input_len % 32))
    // Append length
    x[block_idx] = input_len

    var a: int = 1732584193    // 0x67452301
    var b: int = 4023233417    // 0xEFCDAB89
    var c: int = 2562383102    // 0x98BADCFE
    var d: int = 271733878     // 0x10325476

    var i: int = 0
    while (i < block_idx + 1) {
        var olda = a
        var oldb = b
        var oldc = c
        var oldd = d

        // Use unsigned 32-bit constants (negatives converted from JS)
        a = md5_ff(a, b, c, d, x[i+ 0], 7 , 3614090360)   // -680876936
        d = md5_ff(d, a, b, c, x[i+ 1], 12, 3905402710)   // -389564586
        c = md5_ff(c, d, a, b, x[i+ 2], 17, 606105819)
        b = md5_ff(b, c, d, a, x[i+ 3], 22, 3250441966)   // -1044525330
        a = md5_ff(a, b, c, d, x[i+ 4], 7 , 4118548399)   // -176418897
        d = md5_ff(d, a, b, c, x[i+ 5], 12, 1200080426)
        c = md5_ff(c, d, a, b, x[i+ 6], 17, 2821735955)   // -1473231341
        b = md5_ff(b, c, d, a, x[i+ 7], 22, 4249261313)   // -45705983
        a = md5_ff(a, b, c, d, x[i+ 8], 7 , 1770035416)
        d = md5_ff(d, a, b, c, x[i+ 9], 12, 2336552879)   // -1958414417
        c = md5_ff(c, d, a, b, x[i+10], 17, 4294925233)   // -42063
        b = md5_ff(b, c, d, a, x[i+11], 22, 2304563134)   // -1990404162
        a = md5_ff(a, b, c, d, x[i+12], 7 , 1804603682)
        d = md5_ff(d, a, b, c, x[i+13], 12, 4254626195)   // -40341101
        c = md5_ff(c, d, a, b, x[i+14], 17, 2792965006)   // -1502002290
        b = md5_ff(b, c, d, a, x[i+15], 22, 1236535329)

        a = md5_gg(a, b, c, d, x[i+ 1], 5 , 4129170786)   // -165796510
        d = md5_gg(d, a, b, c, x[i+ 6], 9 , 3225465664)   // -1069501632
        c = md5_gg(c, d, a, b, x[i+11], 14, 643717713)
        b = md5_gg(b, c, d, a, x[i+ 0], 20, 3921069994)   // -373897302
        a = md5_gg(a, b, c, d, x[i+ 5], 5 , 3593408605)   // -701558691
        d = md5_gg(d, a, b, c, x[i+10], 9 , 38016083)
        c = md5_gg(c, d, a, b, x[i+15], 14, 3634488961)   // -660478335
        b = md5_gg(b, c, d, a, x[i+ 4], 20, 3889429448)   // -405537848
        a = md5_gg(a, b, c, d, x[i+ 9], 5 , 568446438)
        d = md5_gg(d, a, b, c, x[i+14], 9 , 3275163606)   // -1019803690
        c = md5_gg(c, d, a, b, x[i+ 3], 14, 4107603335)   // -187363961
        b = md5_gg(b, c, d, a, x[i+ 8], 20, 1163531501)
        a = md5_gg(a, b, c, d, x[i+13], 5 , 2850285829)   // -1444681467
        d = md5_gg(d, a, b, c, x[i+ 2], 9 , 4243563512)   // -51403784
        c = md5_gg(c, d, a, b, x[i+ 7], 14, 1735328473)
        b = md5_gg(b, c, d, a, x[i+12], 20, 2368359562)   // -1926607734

        a = md5_hh(a, b, c, d, x[i+ 5], 4 , 4294588738)   // -378558
        d = md5_hh(d, a, b, c, x[i+ 8], 11, 2272392833)   // -2022574463
        c = md5_hh(c, d, a, b, x[i+11], 16, 1839030562)
        b = md5_hh(b, c, d, a, x[i+14], 23, 4259657740)   // -35309556
        a = md5_hh(a, b, c, d, x[i+ 1], 4 , 2763975236)   // -1530992060
        d = md5_hh(d, a, b, c, x[i+ 4], 11, 1272893353)
        c = md5_hh(c, d, a, b, x[i+ 7], 16, 4139469664)   // -155497632
        b = md5_hh(b, c, d, a, x[i+10], 23, 3200236656)   // -1094730640
        a = md5_hh(a, b, c, d, x[i+13], 4 , 681279174)
        d = md5_hh(d, a, b, c, x[i+ 0], 11, 3936430074)   // -358537222
        c = md5_hh(c, d, a, b, x[i+ 3], 16, 3572445317)   // -722521979
        b = md5_hh(b, c, d, a, x[i+ 6], 23, 76029189)
        a = md5_hh(a, b, c, d, x[i+ 9], 4 , 3654602809)   // -640364487
        d = md5_hh(d, a, b, c, x[i+12], 11, 3873151461)   // -421815835
        c = md5_hh(c, d, a, b, x[i+15], 16, 530742520)
        b = md5_hh(b, c, d, a, x[i+ 2], 23, 3299628645)   // -995338651

        a = md5_ii(a, b, c, d, x[i+ 0], 6 , 4096336452)   // -198630844
        d = md5_ii(d, a, b, c, x[i+ 7], 10, 1126891415)
        c = md5_ii(c, d, a, b, x[i+14], 15, 2878612391)   // -1416354905
        b = md5_ii(b, c, d, a, x[i+ 5], 21, 4237533241)   // -57434055
        a = md5_ii(a, b, c, d, x[i+12], 6 , 1700485571)
        d = md5_ii(d, a, b, c, x[i+ 3], 10, 2399980690)   // -1894986606
        c = md5_ii(c, d, a, b, x[i+10], 15, 4293915773)   // -1051523
        b = md5_ii(b, c, d, a, x[i+ 1], 21, 2240044497)   // -2054922799
        a = md5_ii(a, b, c, d, x[i+ 8], 6 , 1873313359)
        d = md5_ii(d, a, b, c, x[i+15], 10, 4264355552)   // -30611744
        c = md5_ii(c, d, a, b, x[i+ 6], 15, 2734768916)   // -1560198380
        b = md5_ii(b, c, d, a, x[i+13], 21, 1309151649)
        a = md5_ii(a, b, c, d, x[i+ 4], 6 , 4149444226)   // -145523070
        d = md5_ii(d, a, b, c, x[i+11], 10, 3174756917)   // -1120210379
        c = md5_ii(c, d, a, b, x[i+ 2], 15, 718787259)
        b = md5_ii(b, c, d, a, x[i+ 9], 21, 3951481745)   // -343485551

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
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
