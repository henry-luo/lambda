// SHA-256 in Lambda; the round constants come from the first 64 primes.
import .text_encoding

fn mask32(value) => band(value, 4294967295i64)
fn rotr32(value, shift: int) => mask32(bor(shr(value, shift), shl(value, 32 - shift)))

pn first_primes(count: int) {
    var primes = []
    var candidate = 2
    while (len(primes) < count) {
        var composite = false
        for (prime in primes) {
            if (prime * prime <= candidate and candidate % prime == 0) {
                composite = true
            }
        }
        if (not composite) { primes = primes ++ [candidate] }
        candidate = candidate + 1
    }
    return primes
}

pn word_fraction(root: float) {
    return i64(floor((root - floor(root)) * 4294967296.0))^
}

pub pn sha256_hex(source: string) {
    let primes = first_primes(64)
    var constants: i64[] = []
    var digest: i64[] = []
    var i = 0
    for (prime in primes) {
        constants = constants ++ [word_fraction(math.cbrt(float(prime)))]
        if (i < 8) { digest = digest ++ [word_fraction(math.sqrt(float(prime)))] }
        i = i + 1
    }

    var bytes = utf8_bytes(source)
    let bit_length = len(bytes) * 8
    bytes = bytes ++ [128]
    while (len(bytes) % 64 != 56) { bytes = bytes ++ [0] }
    for (shift in [56, 48, 40, 32, 24, 16, 8, 0]) {
        bytes = bytes ++ [band(shr(bit_length, shift), 255)]
    }

    var offset = 0
    while (offset < len(bytes)) {
        var words: i64[] = [for (i in 0 to 63) 0i64]
        var t = 0
        while (t < 16) {
            let base = offset + t * 4
            words[t] = bor(bor(shl(i64(bytes[base]), 24), shl(i64(bytes[base + 1]), 16)),
                           bor(shl(i64(bytes[base + 2]), 8), i64(bytes[base + 3])))
            t = t + 1
        }
        while (t < 64) {
            let x = words[t - 15]
            let y = words[t - 2]
            let sigma0 = bxor(bxor(rotr32(x, 7), rotr32(x, 18)), shr(x, 3))
            let sigma1 = bxor(bxor(rotr32(y, 17), rotr32(y, 19)), shr(y, 10))
            words[t] = mask32(words[t - 16] + sigma0 + words[t - 7] + sigma1)
            t = t + 1
        }

        var a = digest[0]
        var b = digest[1]
        var c = digest[2]
        var d = digest[3]
        var e = digest[4]
        var f = digest[5]
        var g = digest[6]
        var h = digest[7]
        t = 0
        while (t < 64) {
            let big1 = bxor(bxor(rotr32(e, 6), rotr32(e, 11)), rotr32(e, 25))
            let choose = bxor(band(e, f), band(bnot(e), g))
            let first = mask32(h + big1 + choose + constants[t] + words[t])
            let big0 = bxor(bxor(rotr32(a, 2), rotr32(a, 13)), rotr32(a, 22))
            let majority = bxor(bxor(band(a, b), band(a, c)), band(b, c))
            let second = mask32(big0 + majority)
            h = g
            g = f
            f = e
            e = mask32(d + first)
            d = c
            c = b
            b = a
            a = mask32(first + second)
            t = t + 1
        }
        digest[0] = mask32(digest[0] + a)
        digest[1] = mask32(digest[1] + b)
        digest[2] = mask32(digest[2] + c)
        digest[3] = mask32(digest[3] + d)
        digest[4] = mask32(digest[4] + e)
        digest[5] = mask32(digest[5] + f)
        digest[6] = mask32(digest[6] + g)
        digest[7] = mask32(digest[7] + h)
        offset = offset + 64
    }
    return lower(join([for (word in digest) hex8(word)], ""))
}
