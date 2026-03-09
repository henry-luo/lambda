// JetStream Benchmark: crypto-rsa (Octane)
// RSA public/private key encryption/decryption
// Original: Tom Wu 2003-2005 (jsbn BigInteger library)
// Tests: big integer arithmetic, modular exponentiation (Montgomery/Barrett)
//
// BigInteger is represented as map {a: array, t: int, s: int}
//   a = digit array (28 bits per digit, little-endian)
//   t = number of used digits
//   s = sign (0 for non-negative, -1 for negative)

// ================= BigInteger Constants =================
let BI_DB = 28
let BI_DM = 268435455    // (1<<28)-1
let BI_DV = 268435456    // (1<<28)
let BI_FP = 52
let BI_FV = 4503599627370496.0  // 2^52 as float
let BI_F1 = 24           // 52-28
let BI_F2 = 4            // 2*28-52

// Reducer type tags
let RED_CLASSIC    = 0
let RED_MONTGOMERY = 1
let RED_BARRETT    = 2

// Hex digit conversion table
let BI_RM = "0123456789abcdefghijklmnopqrstuvwxyz"

// Reverse lookup table: charCode -> digit value (128 entries)
// '0'-'9' (48-57) -> 0-9, 'A'-'Z' (65-90) -> 10-35, 'a'-'z' (97-122) -> 10-35
let BI_RC = [
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1
]

pn int2char(n: int) {
    return slice(BI_RM, n, n + 1)
}

pn intAt(s: string, i: int) {
    var c = int(ord(slice(s, i, i + 1)))
    if (c < 0 or c >= 128) { return -1 }
    return BI_RC[c]
}

// ================= BigInteger Construction =================

pn nbi() {
    return {a: fill(1, 0), t: 0, s: 0}
}

pn bi_ensure(bi, n: int) {
    // ensure bi.a has at least n+1 elements
    var a = bi.a
    var alen = len(a)
    if (n >= alen) {
        var new_a = fill(n + 16, 0)
        var i: int = 0
        while (i < alen) {
            new_a[i] = a[i]
            i = i + 1
        }
        bi.a = new_a
    }
}

pn bi_from_int(x: int) {
    var r = nbi()
    var ra = r.a
    r.t = 1
    r.s = 0
    if (x > 0) {
        ra[0] = x
    } else {
        if (x < -1) {
            ra[0] = x + BI_DV
            r.s = -1
        } else {
            r.t = 0
            if (x < 0) { r.s = -1 }
        }
    }
    return r
}

// ================= am3: Multiply-Accumulate (28-bit digits) =================
// Compute w_a[j..j+n-1] += bi_a[i..i+n-1] * x, propagate carries
// Returns final carry
pn am3(bi_a, i: int, x: int, w_a, j: int, c: int, n: int) {
    var xl = band(x, 16383)
    var xh = shr(x, 14)
    while (n > 0) {
        n = n - 1
        var l = band(bi_a[i], 16383)
        var h = shr(bi_a[i], 14)
        i = i + 1
        var m = xh * l + h * xl
        l = xl * l + shl(band(m, 16383), 14) + w_a[j] + c
        c = shr(l, 28) + shr(m, 14) + xh * h
        w_a[j] = band(l, BI_DM)
        j = j + 1
    }
    return c
}

// ================= Core BigInteger Operations =================

pn bi_clamp(bi) {
    var a = bi.a
    var c = band(bi.s, BI_DM)
    var t = bi.t
    while (t > 0 and a[t - 1] == c) {
        t = t - 1
    }
    bi.t = t
}

pn bi_copy_to(src, dst) {
    var sa = src.a
    var da = dst.a
    var t = src.t
    bi_ensure(dst, t)
    da = dst.a
    var i = t - 1
    while (i >= 0) {
        da[i] = sa[i]
        i = i - 1
    }
    dst.t = t
    dst.s = src.s
}

pn bi_clone(bi) {
    var r = nbi()
    bi_copy_to(bi, r)
    return r
}

pn bi_from_string(s: string, b: int) {
    var bi = nbi()
    var k: int = 0
    if (b == 16) { k = 4 }
    else { if (b == 8) { k = 3 }
    else { if (b == 256) { k = 8 }
    else { if (b == 2) { k = 1 }
    else { if (b == 32) { k = 5 }
    else { if (b == 4) { k = 2 }
    else { return bi }}}}}}

    bi.t = 0
    bi.s = 0
    var slen = int(len(s))
    var i = slen
    var mi = false
    var sh: int = 0
    bi_ensure(bi, int(slen * 4 / BI_DB) + 2)
    var ba = bi.a
    while (i > 0) {
        i = i - 1
        var x: int = 0
        if (k == 8) {
            x = band(int(ord(slice(s, i, i + 1))), 255)
        } else {
            x = intAt(s, i)
        }
        if (x < 0) {
            if (slice(s, i, i + 1) == "-") { mi = true }
            // skip
        } else {
            mi = false
            if (sh == 0) {
                bi_ensure(bi, bi.t + 1)
                ba = bi.a
                ba[bi.t] = x
                bi.t = bi.t + 1
            } else {
                if (sh + k > BI_DB) {
                    ba[bi.t - 1] = bor(ba[bi.t - 1], shl(band(x, shl(1, BI_DB - sh) - 1), sh))
                    bi_ensure(bi, bi.t + 1)
                    ba = bi.a
                    ba[bi.t] = shr(x, BI_DB - sh)
                    bi.t = bi.t + 1
                } else {
                    ba[bi.t - 1] = bor(ba[bi.t - 1], shl(x, sh))
                }
            }
            sh = sh + k
            if (sh >= BI_DB) { sh = sh - BI_DB }
        }
    }
    if (k == 8 and band(int(ord(slice(s, 0, 1))), 128) != 0) {
        bi.s = -1
        if (sh > 0) {
            ba[bi.t - 1] = bor(ba[bi.t - 1], shl(shl(1, BI_DB - sh) - 1, sh))
        }
    }
    bi_clamp(bi)
    if (mi) {
        var z = bi_from_int(0)
        bi_sub_to(z, bi, bi)
    }
    return bi
}

// Construct BigInteger from byte array (for PKCS#1 padding)
pn bi_from_byte_array(ba) {
    var bi = nbi()
    bi.t = 0
    bi.s = 0
    var i = int(len(ba))
    var sh: int = 0
    bi_ensure(bi, int(i * 8 / BI_DB) + 2)
    var bia = bi.a
    while (i > 0) {
        i = i - 1
        var x = band(ba[i], 255)
        if (sh == 0) {
            bi_ensure(bi, bi.t + 1)
            bia = bi.a
            bia[bi.t] = x
            bi.t = bi.t + 1
        } else {
            if (sh + 8 > BI_DB) {
                bia[bi.t - 1] = bor(bia[bi.t - 1], shl(band(x, shl(1, BI_DB - sh) - 1), sh))
                bi_ensure(bi, bi.t + 1)
                bia = bi.a
                bia[bi.t] = shr(x, BI_DB - sh)
                bi.t = bi.t + 1
            } else {
                bia[bi.t - 1] = bor(bia[bi.t - 1], shl(x, sh))
            }
        }
        sh = sh + 8
        if (sh >= BI_DB) { sh = sh - BI_DB }
    }
    if (band(ba[0], 128) != 0) {
        bi.s = -1
        if (sh > 0) {
            bia[bi.t - 1] = bor(bia[bi.t - 1], shl(shl(1, BI_DB - sh) - 1, sh))
        }
    }
    bi_clamp(bi)
    return bi
}

// ================= Comparison & Bit Operations =================

pn nbits(x: int) {
    var r: int = 1
    var t: int = 0
    t = shr(x, 16)
    if (t != 0) { x = t; r = r + 16 }
    t = shr(x, 8)
    if (t != 0) { x = t; r = r + 8 }
    t = shr(x, 4)
    if (t != 0) { x = t; r = r + 4 }
    t = shr(x, 2)
    if (t != 0) { x = t; r = r + 2 }
    t = shr(x, 1)
    if (t != 0) { r = r + 1 }
    return r
}

pn bi_bit_length(bi) {
    var a = bi.a
    if (bi.t <= 0) { return 0 }
    return BI_DB * (bi.t - 1) + nbits(bxor(a[bi.t - 1], band(bi.s, BI_DM)))
}

pn bi_compare(a, b) {
    var r = a.s - b.s
    if (r != 0) { return r }
    var i = a.t
    r = i - b.t
    if (r != 0) { return r }
    var aa = a.a
    var ba = b.a
    i = i - 1
    while (i >= 0) {
        r = aa[i] - ba[i]
        if (r != 0) { return r }
        i = i - 1
    }
    return 0
}

pn bi_is_even(bi) {
    var a = bi.a
    if (bi.t > 0) {
        return band(a[0], 1) == 0
    }
    return band(bi.s, 1) == 0
}

pn bi_test_bit(bi, n: int) {
    var a = bi.a
    var j = int(n / BI_DB)
    if (j >= bi.t) { return bi.s != 0 }
    return band(a[j], shl(1, n % BI_DB)) != 0
}

pn lbit(x: int) {
    if (x == 0) { return -1 }
    var r: int = 0
    if (band(x, 65535) == 0) { x = shr(x, 16); r = r + 16 }
    if (band(x, 255) == 0) { x = shr(x, 8); r = r + 8 }
    if (band(x, 15) == 0) { x = shr(x, 4); r = r + 4 }
    if (band(x, 3) == 0) { x = shr(x, 2); r = r + 2 }
    if (band(x, 1) == 0) { r = r + 1 }
    return r
}

pn bi_lowest_set_bit(bi) {
    var a = bi.a
    var i: int = 0
    while (i < bi.t) {
        if (a[i] != 0) { return i * BI_DB + lbit(a[i]) }
        i = i + 1
    }
    if (bi.s < 0) { return bi.t * BI_DB }
    return -1
}

pn bi_signum(bi) {
    var a = bi.a
    if (bi.s < 0) { return -1 }
    if (bi.t <= 0 or (bi.t == 1 and a[0] <= 0)) { return 0 }
    return 1
}

pn bi_int_value(bi) {
    var a = bi.a
    if (bi.s < 0) {
        if (bi.t == 1) { return a[0] - BI_DV }
        if (bi.t == 0) { return -1 }
    } else {
        if (bi.t == 1) { return a[0] }
        if (bi.t == 0) { return 0 }
    }
    return bor(shl(band(a[1], shl(1, 32 - BI_DB) - 1), BI_DB), a[0])
}

// ================= Shift Operations =================

pn bi_dl_shift_to(src, n: int, r) {
    var sa = src.a
    bi_ensure(r, src.t + n + 1)
    var ra = r.a
    var i = src.t - 1
    while (i >= 0) {
        ra[i + n] = sa[i]
        i = i - 1
    }
    i = n - 1
    while (i >= 0) {
        ra[i] = 0
        i = i - 1
    }
    r.t = src.t + n
    r.s = src.s
}

pn bi_dr_shift_to(src, n: int, r) {
    var sa = src.a
    var new_t = src.t - n
    if (new_t < 0) { new_t = 0 }
    bi_ensure(r, new_t + 1)
    var ra = r.a
    var i = n
    while (i < src.t) {
        ra[i - n] = sa[i]
        i = i + 1
    }
    r.t = new_t
    r.s = src.s
}

pn bi_l_shift_to(src, n: int, r) {
    var sa = src.a
    var bs = n % BI_DB
    var cbs = BI_DB - bs
    var bm = shl(1, cbs) - 1
    var ds = int(n / BI_DB)
    var c = band(shl(src.s, bs), BI_DM)
    bi_ensure(r, src.t + ds + 2)
    var ra = r.a
    var i = src.t - 1
    while (i >= 0) {
        ra[i + ds + 1] = bor(shr(sa[i], cbs), c)
        c = shl(band(sa[i], bm), bs)
        i = i - 1
    }
    i = ds - 1
    while (i >= 0) {
        ra[i] = 0
        i = i - 1
    }
    ra[ds] = c
    r.t = src.t + ds + 1
    r.s = src.s
    bi_clamp(r)
}

pn bi_r_shift_to(src, n: int, r) {
    var sa = src.a
    r.s = src.s
    var ds = int(n / BI_DB)
    if (ds >= src.t) { r.t = 0; return 0 }
    var bs = n % BI_DB
    var cbs = BI_DB - bs
    var bm = shl(1, bs) - 1
    bi_ensure(r, src.t - ds + 1)
    var ra = r.a
    ra[0] = shr(sa[ds], bs)
    var i = ds + 1
    while (i < src.t) {
        ra[i - ds - 1] = bor(ra[i - ds - 1], shl(band(sa[i], bm), cbs))
        ra[i - ds] = shr(sa[i], bs)
        i = i + 1
    }
    if (bs > 0) {
        ra[src.t - ds - 1] = bor(ra[src.t - ds - 1], shl(band(src.s, bm), cbs))
    }
    r.t = src.t - ds
    bi_clamp(r)
    return 0
}

pn bi_shift_left(bi, n: int) {
    var r = nbi()
    if (n < 0) { bi_r_shift_to(bi, 0 - n, r) }
    else { bi_l_shift_to(bi, n, r) }
    return r
}

pn bi_shift_right(bi, n: int) {
    var r = nbi()
    if (n < 0) { bi_l_shift_to(bi, 0 - n, r) }
    else { bi_r_shift_to(bi, n, r) }
    return r
}

// ================= Arithmetic Operations =================

pn bi_sub_to(a_bi, b_bi, r) {
    var aa = a_bi.a
    var ba = b_bi.a
    bi_ensure(r, a_bi.t + b_bi.t + 2)
    var ra = r.a
    var i: int = 0
    var c: int = 0
    var m = a_bi.t
    if (b_bi.t < m) { m = b_bi.t }
    while (i < m) {
        c = c + aa[i] - ba[i]
        ra[i] = band(c, BI_DM)
        c = shr(c, BI_DB)
        i = i + 1
    }
    if (b_bi.t < a_bi.t) {
        c = c - b_bi.s
        while (i < a_bi.t) {
            c = c + aa[i]
            ra[i] = band(c, BI_DM)
            c = shr(c, BI_DB)
            i = i + 1
        }
        c = c + a_bi.s
    } else {
        c = c + a_bi.s
        while (i < b_bi.t) {
            c = c - ba[i]
            ra[i] = band(c, BI_DM)
            c = shr(c, BI_DB)
            i = i + 1
        }
        c = c - b_bi.s
    }
    if (c < 0) { r.s = -1 } else { r.s = 0 }
    if (c < -1) {
        ra[i] = BI_DV + c
        i = i + 1
    } else {
        if (c > 0) {
            ra[i] = c
            i = i + 1
        }
    }
    r.t = i
    bi_clamp(r)
}

pn bi_add_to(a_bi, b_bi, r) {
    var aa = a_bi.a
    var ba = b_bi.a
    bi_ensure(r, a_bi.t + b_bi.t + 2)
    var ra = r.a
    var i: int = 0
    var c: int = 0
    var m = a_bi.t
    if (b_bi.t < m) { m = b_bi.t }
    while (i < m) {
        c = c + aa[i] + ba[i]
        ra[i] = band(c, BI_DM)
        c = shr(c, BI_DB)
        i = i + 1
    }
    if (b_bi.t < a_bi.t) {
        c = c + b_bi.s
        while (i < a_bi.t) {
            c = c + aa[i]
            ra[i] = band(c, BI_DM)
            c = shr(c, BI_DB)
            i = i + 1
        }
        c = c + a_bi.s
    } else {
        c = c + a_bi.s
        while (i < b_bi.t) {
            c = c + ba[i]
            ra[i] = band(c, BI_DM)
            c = shr(c, BI_DB)
            i = i + 1
        }
        c = c + b_bi.s
    }
    if (c < 0) { r.s = -1 } else { r.s = 0 }
    if (c > 0) {
        ra[i] = c
        i = i + 1
    } else {
        if (c < -1) {
            ra[i] = BI_DV + c
            i = i + 1
        }
    }
    r.t = i
    bi_clamp(r)
}

pn bi_abs(bi) {
    if (bi.s < 0) { return bi_negate(bi) }
    return bi
}

pn bi_negate(bi) {
    var r = nbi()
    var z = bi_from_int(0)
    bi_sub_to(z, bi, r)
    return r
}

pn bi_add(a, b) {
    var r = nbi()
    bi_add_to(a, b, r)
    return r
}

pn bi_subtract(a, b) {
    var r = nbi()
    bi_sub_to(a, b, r)
    return r
}

// ================= Multiplication =================

pn bi_multiply_to(x_bi, y_bi, r) {
    var xabs = bi_abs(x_bi)
    var yabs = bi_abs(y_bi)
    var xa = xabs.a
    var ya = yabs.a
    var i = xabs.t
    var new_t = i + yabs.t
    bi_ensure(r, new_t + 1)
    var ra = r.a
    r.t = new_t
    i = i - 1
    while (i >= 0) {
        ra[i] = 0
        i = i - 1
    }
    i = 0
    while (i < yabs.t) {
        ra[i + xabs.t] = am3(xa, 0, ya[i], ra, i, 0, xabs.t)
        i = i + 1
    }
    r.s = 0
    bi_clamp(r)
    if (x_bi.s != y_bi.s) {
        var z = bi_from_int(0)
        bi_sub_to(z, r, r)
    }
}

pn bi_square_to(x_bi, r) {
    var x = bi_abs(x_bi)
    var xa = x.a
    var new_t = 2 * x.t
    bi_ensure(r, new_t + 1)
    var ra = r.a
    var i = new_t
    r.t = new_t
    i = i - 1
    while (i >= 0) {
        ra[i] = 0
        i = i - 1
    }
    i = 0
    while (i < x.t - 1) {
        var c = am3(xa, i, xa[i], ra, 2 * i, 0, 1)
        var idx = i + x.t
        ra[idx] = ra[idx] + am3(xa, i + 1, 2 * xa[i], ra, 2 * i + 1, c, x.t - i - 1)
        if (ra[idx] >= BI_DV) {
            ra[idx] = ra[idx] - BI_DV
            ra[idx + 1] = 1
        }
        i = i + 1
    }
    if (r.t > 0) {
        ra[r.t - 1] = ra[r.t - 1] + am3(xa, i, xa[i], ra, 2 * i, 0, 1)
    }
    r.s = 0
    bi_clamp(r)
}

// ================= Division =================

pn bi_div_rem_to(bi, m, q, r) {
    var pm = bi_abs(m)
    if (pm.t <= 0) { return 0 }
    var pt = bi_abs(bi)
    if (pt.t < pm.t) {
        if (q != null) { bi_from_int_to(q, 0) }
        if (r != null) { bi_copy_to(bi, r) }
        return 0
    }
    if (r == null) { r = nbi() }
    var y = nbi()
    var ts = bi.s
    var ms = m.s
    var pma = pm.a
    var nsh = BI_DB - nbits(pma[pm.t - 1])
    if (nsh > 0) {
        bi_l_shift_to(pm, nsh, y)
        bi_l_shift_to(pt, nsh, r)
    } else {
        bi_copy_to(pm, y)
        bi_copy_to(pt, r)
    }
    var ys = y.t
    var ya = y.a
    var y0 = ya[ys - 1]
    if (y0 == 0) { return 0 }
    var yt = float(y0) * float(shl(1, BI_F1))
    if (ys > 1) { yt = yt + float(shr(ya[ys - 2], BI_F2)) }
    var d1 = BI_FV / yt
    var d2 = float(shl(1, BI_F1)) / yt
    var e = float(shl(1, BI_F2))
    var i = r.t
    var j = i - ys
    var t_bi = nbi()
    if (q == null) { t_bi = nbi() } else { t_bi = q }
    bi_dl_shift_to(y, j, t_bi)

    var ra = r.a
    if (bi_compare(r, t_bi) >= 0) {
        bi_ensure(r, r.t + 2)
        ra = r.a
        ra[r.t] = 1
        r.t = r.t + 1
        bi_sub_to(r, t_bi, r)
        ra = r.a
    }
    var one = bi_from_int(1)
    bi_dl_shift_to(one, ys, t_bi)
    bi_sub_to(t_bi, y, y)
    ya = y.a
    while (y.t < ys) {
        bi_ensure(y, y.t + 1)
        ya = y.a
        ya[y.t] = 0
        y.t = y.t + 1
    }
    while (j > 0) {
        j = j - 1
        i = i - 1
        ra = r.a
        var qd: int = 0
        if (ra[i] == y0) {
            qd = BI_DM
        } else {
            qd = int(float(ra[i]) * d1 + (float(ra[i - 1]) + e) * d2)
        }
        ra[i] = ra[i] + am3(ya, 0, qd, ra, j, 0, ys)
        if (ra[i] < qd) {
            bi_dl_shift_to(y, j, t_bi)
            bi_sub_to(r, t_bi, r)
            ra = r.a
            qd = qd - 1
            while (ra[i] < qd) {
                bi_sub_to(r, t_bi, r)
                ra = r.a
                qd = qd - 1
            }
        }
    }
    if (q != null) {
        bi_dr_shift_to(r, ys, q)
        if (ts != ms) {
            var z = bi_from_int(0)
            bi_sub_to(z, q, q)
        }
    }
    r.t = ys
    bi_clamp(r)
    if (nsh > 0) { bi_r_shift_to(r, nsh, r) }
    if (ts < 0) {
        var z2 = bi_from_int(0)
        bi_sub_to(z2, r, r)
    }
    return 0
}

pn bi_from_int_to(bi, x: int) {
    var a = bi.a
    bi.t = 1
    bi.s = 0
    if (x > 0) {
        bi_ensure(bi, 1)
        a = bi.a
        a[0] = x
    } else {
        if (x < -1) {
            bi_ensure(bi, 1)
            a = bi.a
            a[0] = x + BI_DV
            bi.s = -1
        } else {
            bi.t = 0
            if (x < 0) { bi.s = -1 }
        }
    }
}

pn bi_mod(a, b) {
    var r = nbi()
    bi_div_rem_to(bi_abs(a), b, null, r)
    if (a.s < 0 and bi_compare(r, bi_from_int(0)) > 0) {
        bi_sub_to(b, r, r)
    }
    return r
}

pn bi_multiply(a, b) {
    var r = nbi()
    bi_multiply_to(a, b, r)
    return r
}

pn bi_divide(a, b) {
    var r = nbi()
    bi_div_rem_to(a, b, r, null)
    return r
}

// ================= Modular Helpers =================

pn bi_d_multiply(bi, n: int) {
    var a = bi.a
    bi_ensure(bi, bi.t + 1)
    a = bi.a
    a[bi.t] = am3(a, 0, n - 1, a, 0, 0, bi.t)
    bi.t = bi.t + 1
    bi_clamp(bi)
}

pn bi_d_add_offset(bi, n: int, w: int) {
    var a = bi.a
    while (bi.t <= w) {
        bi_ensure(bi, bi.t + 1)
        a = bi.a
        a[bi.t] = 0
        bi.t = bi.t + 1
    }
    a = bi.a
    a[w] = a[w] + n
    while (a[w] >= BI_DV) {
        a[w] = a[w] - BI_DV
        w = w + 1
        if (w >= bi.t) {
            bi_ensure(bi, bi.t + 1)
            a = bi.a
            a[bi.t] = 0
            bi.t = bi.t + 1
        }
        a[w] = a[w] + 1
    }
}

pn bi_mod_int(bi, n: int) {
    if (n <= 0) { return 0 }
    var d = BI_DV % n
    var r: int = 0
    if (bi.s < 0) { r = n - 1 } else { r = 0 }
    var a = bi.a
    if (bi.t > 0) {
        if (d == 0) {
            r = a[0] % n
        } else {
            var i = bi.t - 1
            while (i >= 0) {
                r = (d * r + a[i]) % n
                i = i - 1
            }
        }
    }
    return r
}

// ================= inv_digit (for Montgomery) =================

pn bi_inv_digit(bi) {
    var a = bi.a
    if (bi.t < 1) { return 0 }
    var x = a[0]
    if (band(x, 1) == 0) { return 0 }
    var y = band(x, 3)
    y = band(y * (2 - band(x, 15) * y), 15)
    y = band(y * (2 - band(x, 255) * y), 255)
    y = band(y * (2 - band(band(x, 65535) * y, 65535)), 65535)
    y = (y * (2 - x * y % BI_DV)) % BI_DV
    if (y > 0) { return BI_DV - y }
    return 0 - y
}

// ================= ToString =================

pn bi_to_string(bi, b: int) {
    if (bi.s < 0) { return "-" ++ bi_to_string(bi_negate(bi), b) }
    var k: int = 0
    if (b == 16) { k = 4 }
    else { if (b == 8) { k = 3 }
    else { if (b == 2) { k = 1 }
    else { if (b == 32) { k = 5 }
    else { if (b == 4) { k = 2 }
    else { return "0" }}}}}
    var km = shl(1, k) - 1
    var d: int = 0
    var m = false
    var result = ""
    var a = bi.a
    var i = bi.t
    var p = BI_DB - (i * BI_DB) % k
    if (i > 0) {
        i = i - 1
        if (p < BI_DB) {
            d = shr(a[i], p)
            if (d > 0) { m = true; result = result ++ int2char(d) }
        }
        while (i >= 0) {
            if (p < k) {
                d = shl(band(a[i], shl(1, p) - 1), k - p)
                i = i - 1
                p = p + BI_DB - k
                d = bor(d, shr(a[i], p))
            } else {
                p = p - k
                d = band(shr(a[i], p), km)
                if (p <= 0) { p = p + BI_DB; i = i - 1 }
            }
            if (d > 0) { m = true }
            if (m) { result = result ++ int2char(d) }
        }
    }
    if (m) { return result }
    return "0"
}

// ================= ToByteArray =================

pn bi_to_byte_array(bi) {
    var a = bi.a
    var i = bi.t
    var result = fill(1, bi.s)
    var p = BI_DB - (i * BI_DB) % 8
    var d: int = 0
    var k: int = 0
    if (i > 0) {
        i = i - 1
        if (p < BI_DB) {
            d = shr(a[i], p)
            if (d != shr(band(bi.s, BI_DM), p)) {
                result[k] = bor(d, shl(bi.s, BI_DB - p))
                k = k + 1
            }
        }
        while (i >= 0) {
            if (p < 8) {
                d = shl(band(a[i], shl(1, p) - 1), 8 - p)
                i = i - 1
                p = p + BI_DB - 8
                d = bor(d, shr(a[i], p))
            } else {
                p = p - 8
                d = band(shr(a[i], p), 255)
                if (p <= 0) { p = p + BI_DB; i = i - 1 }
            }
            if (band(d, 128) != 0) { d = bor(d, -256) }
            if (k == 0 and band(bi.s, 128) != band(d, 128)) { k = k + 1 }
            if (k > 0 or d != bi.s) {
                // grow result array
                var rlen = len(result)
                if (k >= rlen) {
                    var new_r = fill(rlen * 2 + 16, 0)
                    var ci: int = 0
                    while (ci < rlen) {
                        new_r[ci] = result[ci]
                        ci = ci + 1
                    }
                    result = new_r
                }
                result[k] = d
                k = k + 1
            }
        }
    }
    // trim to actual size
    var final_r = fill(k, 0)
    var ci: int = 0
    while (ci < k) {
        final_r[ci] = result[ci]
        ci = ci + 1
    }
    return final_r
}

// ================= Bitwise Operations (needed for modPow precomp) =================

pn bi_bitwise_to(a_bi, b_bi, op: int, r) {
    // op: 0=and, 1=or, 2=xor
    var aa = a_bi.a
    var ba = b_bi.a
    var m = a_bi.t
    if (b_bi.t < m) { m = b_bi.t }
    bi_ensure(r, a_bi.t + b_bi.t + 2)
    var ra = r.a
    var i: int = 0
    while (i < m) {
        if (op == 0) { ra[i] = band(aa[i], ba[i]) }
        else { if (op == 1) { ra[i] = bor(aa[i], ba[i]) }
        else { ra[i] = bxor(aa[i], ba[i]) }}
        i = i + 1
    }
    if (b_bi.t < a_bi.t) {
        var f = band(b_bi.s, BI_DM)
        while (i < a_bi.t) {
            if (op == 0) { ra[i] = band(aa[i], f) }
            else { if (op == 1) { ra[i] = bor(aa[i], f) }
            else { ra[i] = bxor(aa[i], f) }}
            i = i + 1
        }
        r.t = a_bi.t
    } else {
        var f2 = band(a_bi.s, BI_DM)
        while (i < b_bi.t) {
            if (op == 0) { ra[i] = band(f2, ba[i]) }
            else { if (op == 1) { ra[i] = bor(f2, ba[i]) }
            else { ra[i] = bxor(f2, ba[i]) }}
            i = i + 1
        }
        r.t = b_bi.t
    }
    if (op == 0) { r.s = band(a_bi.s, b_bi.s) }
    else { if (op == 1) { r.s = bor(a_bi.s, b_bi.s) }
    else { r.s = bxor(a_bi.s, b_bi.s) }}
    bi_clamp(r)
}

// ================= Multiply Lower/Upper (for Barrett) =================

pn bi_multiply_lower_to(x_bi, a_bi, n: int, r) {
    var xa = x_bi.a
    var aa = a_bi.a
    var i = x_bi.t + a_bi.t
    if (n < i) { i = n }
    bi_ensure(r, i + 1)
    var ra = r.a
    r.s = 0
    r.t = i
    while (i > 0) {
        i = i - 1
        ra[i] = 0
    }
    var j = r.t - x_bi.t
    while (i < j) {
        ra[i + x_bi.t] = am3(xa, 0, aa[i], ra, i, 0, x_bi.t)
        i = i + 1
    }
    var j2 = a_bi.t
    if (n < j2) { j2 = n }
    while (i < j2) {
        am3(xa, 0, aa[i], ra, i, 0, n - i)
        i = i + 1
    }
    bi_clamp(r)
}

pn bi_multiply_upper_to(x_bi, a_bi, n: int, r) {
    var xa = x_bi.a
    var aa = a_bi.a
    var nn = n - 1
    var i = x_bi.t + a_bi.t - nn
    bi_ensure(r, i + 1)
    var ra = r.a
    r.s = 0
    r.t = i
    i = i - 1
    while (i >= 0) {
        ra[i] = 0
        i = i - 1
    }
    var start = n - x_bi.t
    if (start < 0) { start = 0 }
    i = start
    while (i < a_bi.t) {
        ra[x_bi.t + i - nn] = am3(xa, nn - i, aa[i], ra, 0, 0, x_bi.t + i - nn)
        i = i + 1
    }
    bi_clamp(r)
    bi_dr_shift_to(r, 1, r)
}

// ================= Classic Reducer =================

pn classic_convert(z, x) {
    if (x.s < 0 or bi_compare(x, z.m) >= 0) {
        return bi_mod(x, z.m)
    }
    return x
}

pn classic_revert(z, x) { return x }

pn classic_reduce(z, x) {
    bi_div_rem_to(x, z.m, null, x)
}

pn classic_mul_to(z, x, y, r) {
    bi_multiply_to(x, y, r)
    classic_reduce(z, r)
}

pn classic_sqr_to(z, x, r) {
    bi_square_to(x, r)
    classic_reduce(z, r)
}

// ================= Montgomery Reducer =================

pn mont_new(m) {
    var mp = bi_inv_digit(m)
    return {
        type: RED_MONTGOMERY,
        m: m,
        mp: mp,
        mpl: band(mp, 32767),
        mph: shr(mp, 15),
        um: shl(1, BI_DB - 15) - 1,
        mt2: 2 * m.t
    }
}

pn mont_convert(z, x) {
    var r = nbi()
    bi_dl_shift_to(bi_abs(x), z.m.t, r)
    bi_div_rem_to(r, z.m, null, r)
    if (x.s < 0 and bi_compare(r, bi_from_int(0)) > 0) {
        bi_sub_to(z.m, r, r)
    }
    return r
}

pn mont_revert(z, x) {
    var r = nbi()
    bi_copy_to(x, r)
    mont_reduce(z, r)
    return r
}

pn mont_reduce(z, x) {
    var xa = x.a
    bi_ensure(x, z.mt2 + 2)
    xa = x.a
    while (x.t <= z.mt2) {
        xa[x.t] = 0
        x.t = x.t + 1
    }
    var ma = z.m.a
    var i: int = 0
    while (i < z.m.t) {
        var j2 = band(xa[i], 32767)
        var u0 = band(j2 * z.mpl + band(shr(j2 * z.mph + shr(xa[i], 15) * z.mpl, 0), z.um) * 32768, BI_DM)
        // Workaround: shr(..., 0) is a no-op but matches JS semantics
        var jj = i + z.m.t
        xa[jj] = xa[jj] + am3(ma, 0, u0, xa, i, 0, z.m.t)
        while (xa[jj] >= BI_DV) {
            xa[jj] = xa[jj] - BI_DV
            jj = jj + 1
            bi_ensure(x, jj + 1)
            xa = x.a
            xa[jj] = xa[jj] + 1
        }
        i = i + 1
    }
    bi_clamp(x)
    bi_dr_shift_to(x, z.m.t, x)
    if (bi_compare(x, z.m) >= 0) {
        bi_sub_to(x, z.m, x)
    }
}

pn mont_mul_to(z, x, y, r) {
    bi_multiply_to(x, y, r)
    mont_reduce(z, r)
}

pn mont_sqr_to(z, x, r) {
    bi_square_to(x, r)
    mont_reduce(z, r)
}

// ================= Barrett Reducer =================

pn barrett_new(m) {
    var r2 = nbi()
    var q3 = nbi()
    var one = bi_from_int(1)
    bi_dl_shift_to(one, 2 * m.t, r2)
    var mu = bi_divide(r2, m)
    return {
        type: RED_BARRETT,
        m: m,
        mu: mu,
        r2: nbi(),
        q3: nbi()
    }
}

pn barrett_convert(z, x) {
    if (x.s < 0 or x.t > 2 * z.m.t) { return bi_mod(x, z.m) }
    if (bi_compare(x, z.m) < 0) { return x }
    var r = nbi()
    bi_copy_to(x, r)
    barrett_reduce(z, r)
    return r
}

pn barrett_revert(z, x) { return x }

pn barrett_reduce(z, x) {
    bi_dr_shift_to(x, z.m.t - 1, z.r2)
    if (x.t > z.m.t + 1) { x.t = z.m.t + 1; bi_clamp(x) }
    bi_multiply_upper_to(z.mu, z.r2, z.m.t + 1, z.q3)
    bi_multiply_lower_to(z.m, z.q3, z.m.t + 1, z.r2)
    while (bi_compare(x, z.r2) < 0) {
        bi_d_add_offset(x, 1, z.m.t + 1)
    }
    bi_sub_to(x, z.r2, x)
    while (bi_compare(x, z.m) >= 0) {
        bi_sub_to(x, z.m, x)
    }
}

pn barrett_mul_to(z, x, y, r) {
    bi_multiply_to(x, y, r)
    barrett_reduce(z, r)
}

pn barrett_sqr_to(z, x, r) {
    bi_square_to(x, r)
    barrett_reduce(z, r)
}

// ================= Reducer Dispatch =================

pn red_convert(z, x) {
    if (z.type == RED_CLASSIC) { return classic_convert(z, x) }
    if (z.type == RED_MONTGOMERY) { return mont_convert(z, x) }
    return barrett_convert(z, x)
}

pn red_revert(z, x) {
    if (z.type == RED_CLASSIC) { return classic_revert(z, x) }
    if (z.type == RED_MONTGOMERY) { return mont_revert(z, x) }
    return barrett_revert(z, x)
}

pn red_sqr_to(z, x, r) {
    if (z.type == RED_CLASSIC) { classic_sqr_to(z, x, r) }
    else { if (z.type == RED_MONTGOMERY) { mont_sqr_to(z, x, r) }
    else { barrett_sqr_to(z, x, r) }}
}

pn red_mul_to(z, x, y, r) {
    if (z.type == RED_CLASSIC) { classic_mul_to(z, x, y, r) }
    else { if (z.type == RED_MONTGOMERY) { mont_mul_to(z, x, y, r) }
    else { barrett_mul_to(z, x, y, r) }}
}

// ================= Exponentiation =================

// exp: this^e, e < 2^32, using reducer z (HAC 14.79)
pn bi_exp(base, e: int, z) {
    if (e > 4294967295 or e < 1) { return bi_from_int(1) }
    var r = nbi()
    var r2 = nbi()
    var g = red_convert(z, base)
    var i = nbits(e) - 1
    bi_copy_to(g, r)
    i = i - 1
    while (i >= 0) {
        red_sqr_to(z, r, r2)
        if (band(e, shl(1, i)) > 0) {
            red_mul_to(z, r2, g, r)
        } else {
            var tmp = r
            r = r2
            r2 = tmp
        }
        i = i - 1
    }
    return red_revert(z, r)
}

// modPowInt: this^e % m, 0 <= e < 2^32
pn bi_mod_pow_int(base, e: int, m) {
    var z = null
    if (e < 256 or bi_is_even(m)) {
        z = {type: RED_CLASSIC, m: m}
    } else {
        z = mont_new(m)
    }
    return bi_exp(base, e, z)
}

// modPow: this^e % m (HAC 14.85, sliding window)
pn bi_mod_pow(base, e_bi, m) {
    var ea = e_bi.a
    var i = bi_bit_length(e_bi)
    var k: int = 0
    var r = bi_from_int(1)
    if (i <= 0) { return r }
    if (i < 18) { k = 1 }
    else { if (i < 48) { k = 3 }
    else { if (i < 144) { k = 4 }
    else { if (i < 768) { k = 5 }
    else { k = 6 }}}}

    var z = null
    if (i < 8) {
        z = {type: RED_CLASSIC, m: m}
    } else {
        if (bi_is_even(m)) {
            z = barrett_new(m)
        } else {
            z = mont_new(m)
        }
    }

    // precomputation: g[1], g[3], g[5], ..., g[km]
    var k1 = k - 1
    var km = shl(1, k) - 1
    var g = fill(km + 1, null)
    g[1] = red_convert(z, base)
    if (k > 1) {
        var g2 = nbi()
        red_sqr_to(z, g[1], g2)
        var n = 3
        while (n <= km) {
            g[n] = nbi()
            red_mul_to(z, g2, g[n - 2], g[n])
            n = n + 2
        }
    }

    var j = e_bi.t - 1
    var w: int = 0
    var is1 = true
    var r2 = nbi()
    i = nbits(ea[j]) - 1

    while (j >= 0) {
        if (i >= k1) {
            w = band(shr(ea[j], i - k1), km)
        } else {
            w = band(shl(ea[j], k1 - i + 1) - shl(ea[j], k1 - i), 0)
            // recreate: w = (ea[j] & ((1<<(i+1))-1)) << (k1-i)
            w = shl(band(ea[j], shl(1, i + 1) - 1), k1 - i)
            if (j > 0) {
                w = bor(w, shr(ea[j - 1], BI_DB + i - k1))
            }
        }

        var n2 = k
        while (band(w, 1) == 0) {
            w = shr(w, 1)
            n2 = n2 - 1
        }
        i = i - n2
        if (i < 0) { i = i + BI_DB; j = j - 1 }

        if (is1) {
            bi_copy_to(g[w], r)
            is1 = false
        } else {
            while (n2 > 1) {
                red_sqr_to(z, r, r2)
                red_sqr_to(z, r2, r)
                n2 = n2 - 2
            }
            if (n2 > 0) {
                red_sqr_to(z, r, r2)
            } else {
                var tmp2 = r
                r = r2
                r2 = tmp2
            }
            red_mul_to(z, r2, g[w], r)
        }

        while (j >= 0 and band(ea[j], shl(1, i)) == 0) {
            red_sqr_to(z, r, r2)
            var tmp3 = r
            r = r2
            r2 = tmp3
            i = i - 1
            if (i < 0) { i = BI_DB - 1; j = j - 1 }
        }
    }
    return red_revert(z, r)
}

// ================= Arcfour PRNG =================

pn arc4_new() {
    return {i: 0, j: 0, s: fill(256, 0)}
}

pn arc4_init(ctx, key) {
    var s_arr = ctx.s
    var i: int = 0
    while (i < 256) {
        s_arr[i] = i
        i = i + 1
    }
    var j: int = 0
    var klen = int(len(key))
    i = 0
    while (i < 256) {
        j = band(j + s_arr[i] + key[i % klen], 255)
        var tmp = s_arr[i]
        s_arr[i] = s_arr[j]
        s_arr[j] = tmp
        i = i + 1
    }
    ctx.i = 0
    ctx.j = 0
}

pn arc4_next(ctx) {
    var s_arr = ctx.s
    ctx.i = band(ctx.i + 1, 255)
    ctx.j = band(ctx.j + s_arr[ctx.i], 255)
    var tmp = s_arr[ctx.i]
    s_arr[ctx.i] = s_arr[ctx.j]
    s_arr[ctx.j] = tmp
    return s_arr[band(tmp + s_arr[ctx.i], 255)]
}

// ================= RNG =================

pn rng_new() {
    var pool = fill(256, 0)
    var pptr: int = 0
    // seed pool deterministically (matches JS benchmark behavior)
    var i: int = 0
    while (i < 256) {
        // use LCG-style deterministic seeding matching JS Math.random() sequence
        var t = int(65536 * (float(i * 1103515245 + 12345) / 2147483648.0)) % 65536
        if (t < 0) { t = 0 - t }
        pool[pptr] = band(shr(t, 8), 255)
        pptr = pptr + 1
        pool[pptr] = band(t, 255)
        pptr = pptr + 1
        if (pptr >= 256) { pptr = pptr - 256 }
        i = i + 1
    }
    // mix in fixed timestamp (same as JS benchmark)
    var ts = 1122926989487
    pool[pptr] = bxor(pool[pptr], band(ts, 255)); pptr = pptr + 1
    pool[pptr] = bxor(pool[pptr], band(shr(ts, 8), 255)); pptr = pptr + 1
    pool[pptr] = bxor(pool[pptr], band(shr(ts, 16), 255)); pptr = pptr + 1
    pool[pptr] = bxor(pool[pptr], band(shr(ts, 24), 255)); pptr = pptr + 1

    var state = arc4_new()
    arc4_init(state, pool)
    return state
}

// ================= PKCS#1 v1.5 Padding =================

pn pkcs1pad2(s: string, n: int, rng_state) {
    if (n < int(len(s)) + 11) {
        return null
    }
    var ba = fill(n, 0)
    var i = int(len(s)) - 1
    while (i >= 0 and n > 0) {
        n = n - 1
        ba[n] = int(ord(slice(s, i, i + 1)))
        i = i - 1
    }
    n = n - 1
    ba[n] = 0
    while (n > 2) {
        n = n - 1
        var x: int = 0
        while (x == 0) {
            x = arc4_next(rng_state)
        }
        ba[n] = x
    }
    n = n - 1
    ba[n] = 2
    n = n - 1
    ba[n] = 0
    return bi_from_byte_array(ba)
}

pn pkcs1unpad2(d, n: int) {
    var b = bi_to_byte_array(d)
    var blen = int(len(b))
    var i: int = 0
    while (i < blen and b[i] == 0) {
        i = i + 1
    }
    if (blen - i != n - 1 or b[i] != 2) {
        return null
    }
    i = i + 1
    while (b[i] != 0) {
        i = i + 1
        if (i >= blen) { return null }
    }
    var ret = ""
    i = i + 1
    while (i < blen) {
        var c = b[i]
        if (c < 0) { c = c + 256 }
        ret = ret ++ chr(c)
        i = i + 1
    }
    return ret
}

// ================= RSA Operations =================

pn rsa_set_public(key, n_hex: string, e_hex: string) {
    key.n = bi_from_string(n_hex, 16)
    // parse e_hex as integer (small public exponent, typically 65537)
    var e_val: int = 0
    var i: int = 0
    var elen = int(len(e_hex))
    while (i < elen) {
        e_val = e_val * 16 + intAt(e_hex, i)
        i = i + 1
    }
    key.e = e_val
}

pn rsa_set_private_ex(key, n_hex: string, e_hex: string, d_hex: string,
                       p_hex: string, q_hex: string, dp_hex: string,
                       dq_hex: string, c_hex: string) {
    rsa_set_public(key, n_hex, e_hex)
    key.d = bi_from_string(d_hex, 16)
    key.p = bi_from_string(p_hex, 16)
    key.q = bi_from_string(q_hex, 16)
    key.dmp1 = bi_from_string(dp_hex, 16)
    key.dmq1 = bi_from_string(dq_hex, 16)
    key.coeff = bi_from_string(c_hex, 16)
}

pn rsa_do_public(key, x) {
    return bi_mod_pow_int(x, key.e, key.n)
}

pn rsa_do_private(key, x) {
    if (key.p == null or key.q == null) {
        return bi_mod_pow(x, key.d, key.n)
    }
    // CRT path
    var xp = bi_mod_pow(bi_mod(x, key.p), key.dmp1, key.p)
    var xq = bi_mod_pow(bi_mod(x, key.q), key.dmq1, key.q)
    while (bi_compare(xp, xq) < 0) {
        xp = bi_add(xp, key.p)
    }
    // xp.subtract(xq).multiply(coeff).mod(p).multiply(q).add(xq)
    var diff = bi_subtract(xp, xq)
    var prod = bi_multiply(diff, key.coeff)
    var rem = bi_mod(prod, key.p)
    var scaled = bi_multiply(rem, key.q)
    return bi_add(scaled, xq)
}

pn rsa_encrypt(key, text: string, rng_state) {
    var n_bytes = int((bi_bit_length(key.n) + 7) / 8)
    var m = pkcs1pad2(text, n_bytes, rng_state)
    if (m == null) { return null }
    var c = rsa_do_public(key, m)
    if (c == null) { return null }
    var h = bi_to_string(c, 16)
    var hlen = int(len(h))
    if (band(hlen, 1) == 0) { return h }
    return "0" ++ h
}

pn rsa_decrypt(key, ctext: string) {
    var c = bi_from_string(ctext, 16)
    var m = rsa_do_private(key, c)
    if (m == null) { return null }
    var n_bytes = int((bi_bit_length(key.n) + 7) / 8)
    return pkcs1unpad2(m, n_bytes)
}

// ================= RSA Key Constants (from Octane benchmark) =================

let nValue  = "a5261939975948bb7a58dffe5ff54e65f0498f9175f5a09288810b8975871e99af3b5dd94057b0fc07535f5f97444504fa35169d461d0d30cf0192e307727c065168c788771c561a9400fb49175e9e6aa4e23fe11af69e9412dd23b0cb6684c4c2429bce139e848ab26d0829073351f4acd36074eafd036a5eb83359d2a698d3"
let eValue  = "10001"
let dValue  = "8e9912f6d3645894e8d38cb58c0db81ff516cf4c7e5a14c7f1eddb1459d2cded4d8d293fc97aee6aefb861859c8b6a3d1dfe710463e1f9ddc72048c09751971c4a580aa51eb523357a3cc48d31cfad1d4a165066ed92d4748fb6571211da5cb14bc11b6e2df7c1a559e6d5ac1cd5c94703a22891464fba23d0d965086277a161"
let pValue  = "d090ce58a92c75233a6486cb0a9209bf3583b64f540c76f5294bb97d285eed33aec220bde14b2417951178ac152ceab6da7090905b478195498b352048f15e7d"
let qValue  = "cab575dc652bb66df15a0359609d51d1db184750c00c6698b90ef3465c99655103edbf0d54c56aec0ce3c4d22592338092a126a0cc49f65a4a30d222b411e58f"
let dmp1Value = "1a24bca8e273df2f0e47c199bbf678604e7df7215480c77c8db39f49b000ce2cf7500038acfff5433b7d582a01f1826e6f4d42e1c57f5e1fef7b12aabc59fd25"
let dmq1Value = "3d06982efbbe47339e1f6d36b1216b8a741d410b0c662f54f7118b27b9a4ec9d914337eb39841d8666f3034408cf94f5b62f11c402fc994fe15a05493150d9fd"
let coeffValue = "3a3e731acd8960b7ff9eb81a7ff93bd1cfa74cbd56987db58b4594fb09c09084db1734c8143f98b602b981aaa9243ca28deb69b5b280ee8dcee0fd2625e53250"

let TEXT = "The quick brown fox jumped over the extremely lazy frog! Now is the time for all good men to come to the party."

// ================= Main Benchmark =================

pn run() {
    var rng_state = rng_new()
    var key = {n: null, e: 0, d: null, p: null, q: null, dmp1: null, dmq1: null, coeff: null}
    rsa_set_public(key, nValue, eValue)
    rsa_set_private_ex(key, nValue, eValue, dValue, pValue, qValue, dmp1Value, dmq1Value, coeffValue)

    // Encrypt
    var encrypted = rsa_encrypt(key, TEXT, rng_state)
    if (encrypted == null) { return false }

    // Decrypt
    var decrypted = rsa_decrypt(key, encrypted)
    if (decrypted == null) { return false }

    return decrypted == TEXT
}

pn main() {
    var __t0 = clock()
    var pass = true
    var iterations = 20
    var i = 0
    while (i < iterations) {
        if (run() == false) {
            pass = false
        }
        i = i + 1
    }
    var __t1 = clock()
    if (pass) {
        print("crypto-rsa: PASS\n")
    } else {
        print("crypto-rsa: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
