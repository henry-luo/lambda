# port of test/benchmark/jetstream/crypto_sha1.py; see ../LICENSE.md.
# JetStream Benchmark: crypto-sha1 (SunSpider) — Julia version
# SHA-1 hash implementation
# Original: Paul Johnston 2000-2002 (FIPS PUB 180-1)
# Tests bitwise operations, array manipulation, and string processing
#
const CHRSZ = 8
const MASK32 = 4294967295
function safe_add(x, y)
    return (add0(x, y) & MASK32)
end

function rol(num, cnt)
    local n
    n = (num & MASK32)
    return (((n << cnt) | (n >> (32 - cnt))) & MASK32)
end

function sha1_ft(t, b, c, d)
    if truth0(((t < 20)))
        return ((b & c) | ((~(b) & MASK32) & d))
    end
    if truth0(((t < 40)))
        return xor(xor(b, c), d)
    end
    if truth0(((t < 60)))
        return (((b & c) | (b & d)) | (c & d))
    end
    return xor(xor(b, c), d)
end

function sha1_kt(t)
    if truth0(((t < 20)))
        return 1518500249
    end
    if truth0(((t < 40)))
        return 1859775393
    end
    if truth0(((t < 60)))
        return 2400959708
    end
    return 3395469782
end

function str2binb(s)
    s = isascii(s) ? AsciiText(s) : s
    local bin_len, binarray, bit_pos, ch, char_idx, i, mask, slen, word_idx
    slen = Base.length(s)
    bin_len = add0((mul0(slen, CHRSZ) >> 5), 1)
    binarray = mul0([0], add0(bin_len, 1))
    mask = ((1 << CHRSZ) - 1)
    for i in range0(0, mul0(slen, CHRSZ), CHRSZ)
        char_idx = fld(i, CHRSZ)
        ch = ord0(get0(s, char_idx))
        word_idx = (i >> 5)
        bit_pos = ((32 - CHRSZ) - mod(i, 32))
        set0!(binarray, word_idx, (get0(binarray, word_idx) | ((ch & mask) << bit_pos)))
    end
    return binarray
end

function core_sha1(x_in, input_len)
    local a, b, c, d, e, i, j, len_idx, olda, oldb, oldc, oldd, olde, pad_idx, padded_len, t, total_len, w, x, x_len
    x_len = Base.length(x_in)
    padded_len = (add0(input_len, 64) >> 9)
    total_len = add0(add0((padded_len << 4), 16), 1)
    if truth0(((total_len < add0(x_len, 20))))
        total_len = add0(x_len, 20)
    end
    x = mul0([0], total_len)
    for i in range0(x_len)
        set0!(x, i, get0(x_in, i))
    end
    pad_idx = (input_len >> 5)
    set0!(x, pad_idx, (get0(x, pad_idx) | (128 << (24 - mod(input_len, 32)))))
    len_idx = add0((padded_len << 4), 15)
    set0!(x, len_idx, input_len)
    w = mul0([0], 80)
    a = 1732584193
    b = 4023233417
    c = 2562383102
    d = 271733878
    e = 3285377520
    i = 0
    while truth0(((i <= len_idx)))
        (olda, oldb, oldc, oldd, olde) = (a, b, c, d, e)
        for j in range0(80)
            if truth0(((j < 16)))
                set0!(w, j, get0(x, add0(i, j)))
            else
                set0!(w, j, rol(xor(xor(xor(get0(w, (j - 3)), get0(w, (j - 8))), get0(w, (j - 14))), get0(w, (j - 16))), 1))
            end
            t = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)), safe_add(safe_add(e, get0(w, j)), sha1_kt(j)))
            e = d
            d = c
            c = rol(b, 30)
            b = a
            a = t
        end
        a = safe_add(a, olda)
        b = safe_add(b, oldb)
        c = safe_add(c, oldc)
        d = safe_add(d, oldd)
        e = safe_add(e, olde)
        i = add0(i, 16)
    end
    return [a, b, c, d, e]
end

function binb2hex(binarray)
    local byte_shift, hex_chars, hi, i, lo, result, word_idx
    hex_chars = "0123456789abcdef"
    result = Any[]
    for i in range0(mul0(Base.length(binarray), 4))
        word_idx = (i >> 2)
        byte_shift = mul0((3 - mod(i, 4)), 8)
        hi = ((get0(binarray, word_idx) >> add0(byte_shift, 4)) & 15)
        lo = ((get0(binarray, word_idx) >> byte_shift) & 15)
        m_append(result, get0(hex_chars, hi))
        m_append(result, get0(hex_chars, lo))
    end
    return m_join("", result)
end

function hex_sha1(s)
    local hash_arr, words
    words = str2binb(s)
    hash_arr = core_sha1(words, mul0(Base.length(s), CHRSZ))
    return binb2hex(hash_arr)
end

function run()
    local _, expected, plain_text, sha1_output
    plain_text = "Two households, both alike in dignity,\nIn fair Verona, where we lay our scene,\nFrom ancient grudge break to new mutiny,\nWhere civil blood makes civil hands unclean.\nFrom forth the fatal loins of these two foes\nA pair of star-cross'd lovers take their life;\nWhole misadventured piteous overthrows\nDo with their death bury their parents' strife.\nThe fearful passage of their death-mark'd love,\nAnd the continuance of their parents' rage,\nWhich, but their children's end, nought could remove,\nIs now the two hours' traffic of our stage;\nThe which if you with patient ears attend,\nWhat here shall miss, our toil shall strive to mend."
    for _ in range0(4)
        plain_text = add0(plain_text, plain_text)
    end
    sha1_output = hex_sha1(plain_text)
    expected = "2524d264def74cce2498bf112bedf00e6c0b796d"
    return ((sha1_output == expected))
end

