// MD5 is required for pdfTeX-compatible checksums, not authentication.
let constants = [3614090360, 3905402710, 606105819, 3250441966, 4118548399, 1200080426, 2821735955, 4249261313, 1770035416, 2336552879, 4294925233, 2304563134, 1804603682, 4254626195, 2792965006, 1236535329, 4129170786, 3225465664, 643717713, 3921069994, 3593408605, 38016083, 3634488961, 3889429448, 568446438, 3275163606, 4107603335, 1163531501, 2850285829, 4243563512, 1735328473, 2368359562, 4294588738, 2272392833, 1839030562, 4259657740, 2763975236, 1272893353, 4139469664, 3200236656, 681279174, 3936430074, 3572445317, 76029189, 3654602809, 3873151461, 530742520, 3299628645, 4096336452, 1126891415, 2878612391, 4237533241, 1700485571, 2399980690, 4293915773, 2240044497, 1873313359, 4264355552, 2734768916, 1309151649, 4149444226, 3174756917, 718787259, 3951481745]
let shifts = [7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21]
let modulus = 4294967296
let mask = 4294967295

fn rotate(x, shift) any^ => bor(shl(x % shl(1, 32 - shift), shift), shr(x, 32 - shift))
fn word(bytes, index) => bytes[index] + bytes[index + 1] * 256 +
    bytes[index + 2] * 65536 + bytes[index + 3] * 16777216

fn round(words, i, a, b, c, d) any^ {
    if (i == 64) [a, b, c, d]
    else {
        let f = if (i < 16) bor(band(b, c), band(bxor(b, mask), d))
            else if (i < 32) bor(band(d, b), band(bxor(d, mask), c))
            else if (i < 48) bxor(bxor(b, c), d)
            else bxor(c, bor(b, bxor(d, mask)));
        let g = if (i < 16) i else if (i < 32) (5 * i + 1) % 16
            else if (i < 48) (3 * i + 5) % 16 else (7 * i) % 16;
        let next_b = (b + rotate((a + f + constants[i] + words[g]) % modulus, shifts[i])^) % modulus;
        round(words, i + 1, d, next_b, b, c)
    }
}

fn blocks(bytes, position, accum) any^ {
    if (position >= len(bytes)) accum
    else {
        let words = [for (i in 0 to 15) word(bytes, position + 4 * i)];
        let next = round(words, 0, accum[0], accum[1], accum[2], accum[3])^;
        blocks(bytes, position + 64, [for (i in 0 to 3) (accum[i] + next[i]) % modulus])
    }
}

fn length_bytes(n) any^ {
    let lo = (n % 536870912) * 8;
    let hi = n div 536870912;
    [lo % 256, (lo div 256) % 256, (lo div 65536) % 256, (lo div 16777216) % 256,
        hi % 256, (hi div 256) % 256, (hi div 65536) % 256, (hi div 16777216) % 256]
}
fn hex_byte(n) any^ => slice("0123456789ABCDEF", n div 16, n div 16 + 1) ++
    slice("0123456789ABCDEF", n % 16, n % 16 + 1)

pub fn digest(text) any^ {
    let bytes = [for (b in binary(text)^) b];
    let padding = (56 - ((len(bytes) + 1) % 64) + 64) % 64;
    let message = [*bytes, 128, *(if (padding == 0) [] else [for (i in 1 to padding) 0]), *length_bytes(len(bytes))^];
    let accum = blocks(message, 0, [1732584193, 4023233417, 2562383102, 271733878])^;
    join([for (word in accum, divisor in [1, 256, 65536, 16777216]) hex_byte((word div divisor) % 256)^], "")
}
