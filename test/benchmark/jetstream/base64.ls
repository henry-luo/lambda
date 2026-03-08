// JetStream Benchmark: base64 (SunSpider)
// Base64 encode/decode
// Original: Mozilla XML-RPC Client (Martijn Pieters, Samuel Sieb)
// Tests string manipulation, character operations, and bitwise ops
// Optimized: Pure integer byte array operations throughout (no string ops in hot path)

// Base64 encoding table as ASCII codes
let ENC = [65,66,67,68,69,70,71,72,73,74,75,76,77,
           78,79,80,81,82,83,84,85,86,87,88,89,90,
           97,98,99,100,101,102,103,104,105,106,107,108,109,
           110,111,112,113,114,115,116,117,118,119,120,121,122,
           48,49,50,51,52,53,54,55,56,57,43,47]

let PAD = 61  // ASCII '='

// Decode table: ASCII code -> base64 value (-1 = invalid)
let DEC = [
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1, 0,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
]

// Encode byte array to base64 byte array
// Returns [out_bytes, out_len]
pn b64_encode(bytes, nbytes: int) {
    var out_len: int = ((nbytes + 2) / 3) * 4
    var out = fill(out_len, 0)
    var oi: int = 0
    var i: int = 0
    while (i + 2 < nbytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        var b2 = bytes[i + 2]
        out[oi] = ENC[shr(b0, 2)]
        out[oi + 1] = ENC[band(shl(band(b0, 3), 4) + shr(b1, 4), 63)]
        out[oi + 2] = ENC[band(shl(band(b1, 15), 2) + shr(b2, 6), 63)]
        out[oi + 3] = ENC[band(b2, 63)]
        oi = oi + 4
        i = i + 3
    }

    // Handle remaining 1 or 2 bytes with padding
    if (nbytes % 3 == 1) {
        var b0 = bytes[i]
        out[oi] = ENC[shr(b0, 2)]
        out[oi + 1] = ENC[shl(band(b0, 3), 4)]
        out[oi + 2] = PAD
        out[oi + 3] = PAD
        oi = oi + 4
    }
    if (nbytes % 3 == 2) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        out[oi] = ENC[shr(b0, 2)]
        out[oi + 1] = ENC[band(shl(band(b0, 3), 4) + shr(b1, 4), 63)]
        out[oi + 2] = ENC[shl(band(b1, 15), 2)]
        out[oi + 3] = PAD
        oi = oi + 4
    }
    return [out, oi]
}

// Decode base64 byte array to original byte array
// Returns [out_bytes, out_len]
pn b64_decode(enc, enc_len: int) {
    var max_out: int = enc_len * 3 / 4
    var out = fill(max_out, 0)
    var oi: int = 0
    var leftbits: int = 0
    var leftdata: int = 0

    var i: int = 0
    while (i < enc_len) {
        var ch = enc[i]
        var padding = (ch == PAD)
        var c = DEC[band(ch, 127)]
        // Skip illegal characters
        if (c == -1) {
            i = i + 1
            continue
        }

        // Collect data into leftdata, update bitcount
        leftdata = bor(shl(leftdata, 6), c)
        leftbits = leftbits + 6

        // If we have 8 or more bits, emit a byte
        if (leftbits >= 8) {
            leftbits = leftbits - 8
            if (padding == false) {
                out[oi] = band(shr(leftdata, leftbits), 255)
                oi = oi + 1
            }
            leftdata = band(leftdata, shl(1, leftbits) - 1)
        }
        i = i + 1
    }

    return [out, oi]
}

// Compare two byte arrays for equality
pn bytes_equal(a, b, n) {
    var i: int = 0
    while (i < n) {
        if (a[i] != b[i]) {
            return false
        }
        i = i + 1
    }
    return true
}

pn run() {
    // Build random byte array directly (no string allocation)
    var n: int = 8192
    var bytes = fill(n, 0)
    var seed = 12345
    var i: int = 0
    while (i < n) {
        // Park-Miller PRNG
        var hi: int = seed / 127773
        var lo = seed % 127773
        seed = 16807 * lo - 2836 * hi
        if (seed <= 0) {
            seed = seed + 2147483647
        }
        var r: int = seed % 26
        bytes[i] = r + 97
        i = i + 1
    }

    while (n <= 16384) {
        var enc_result = b64_encode(bytes, n)
        var enc_bytes = enc_result[0]
        var enc_len = enc_result[1]

        var dec_result = b64_decode(enc_bytes, enc_len)
        var dec_bytes = dec_result[0]
        var dec_len = dec_result[1]

        if (dec_len != n) {
            print("base64: FAIL - length mismatch: expected " ++ string(n) ++ " got " ++ string(dec_len) ++ "\n")
            return false
        }
        if (bytes_equal(bytes, dec_bytes, n) == false) {
            print("base64: FAIL - decode mismatch at size " ++ string(n) ++ "\n")
            return false
        }

        // Double the byte array
        var new_n: int = n * 2
        var new_bytes = fill(new_n, 0)
        i = 0
        while (i < n) {
            new_bytes[i] = bytes[i]
            new_bytes[i + n] = bytes[i]
            i = i + 1
        }
        bytes = new_bytes
        n = new_n
    }
    return true
}

pn main() {
    var __t0 = clock()
    // JetStream runs 8 iterations
    var pass = true
    var iter: int = 0
    while (iter < 8) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("base64: PASS\n")
    } else {
        print("base64: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
