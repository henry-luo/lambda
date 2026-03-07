// JetStream Benchmark: base64 (SunSpider)
// Base64 encode/decode
// Original: Mozilla XML-RPC Client (Martijn Pieters, Samuel Sieb)
// Tests string manipulation, character operations, and bitwise ops

let to_base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
let base64_pad = "="

let MASK32 = 4294967295

pn to_base64(data) {
    var result = ""
    var length = len(data)
    var i: int = 0
    // Convert every three bytes to 4 ascii characters
    while (i < length - 2) {
        var c0 = ord(slice(data, i, i + 1))
        var c1 = ord(slice(data, i + 1, i + 2))
        var c2 = ord(slice(data, i + 2, i + 3))
        result = result ++ slice(to_base64_table, shr(c0, 2), shr(c0, 2) + 1)
        var idx1 = band(shl(band(c0, 3), 4) + shr(c1, 4), MASK32)
        result = result ++ slice(to_base64_table, idx1, idx1 + 1)
        var idx2 = band(shl(band(c1, 15), 2) + shr(c2, 6), MASK32)
        result = result ++ slice(to_base64_table, idx2, idx2 + 1)
        var idx3 = band(c2, 63)
        result = result ++ slice(to_base64_table, idx3, idx3 + 1)
        i = i + 3
    }

    // Convert remaining 1 or 2 bytes, pad out to 4 characters
    if (length % 3 != 0) {
        i = length - (length % 3)
        var c0 = ord(slice(data, i, i + 1))
        result = result ++ slice(to_base64_table, shr(c0, 2), shr(c0, 2) + 1)
        if (length % 3 == 2) {
            var c1 = ord(slice(data, i + 1, i + 2))
            var idx1 = band(shl(band(c0, 3), 4) + shr(c1, 4), MASK32)
            result = result ++ slice(to_base64_table, idx1, idx1 + 1)
            var idx2 = shl(band(c1, 15), 2)
            result = result ++ slice(to_base64_table, idx2, idx2 + 1)
            result = result ++ base64_pad
        } else {
            var idx1 = shl(band(c0, 3), 4)
            result = result ++ slice(to_base64_table, idx1, idx1 + 1)
            result = result ++ base64_pad ++ base64_pad
        }
    }

    return result
}

// Lookup table: ASCII code -> base64 value (-1 = invalid)
let to_binary_table = [
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1, 0,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
]

pn base64_to_string(data) {
    var result = ""
    var leftbits: int = 0
    var leftdata: int = 0
    let pad_code = ord(base64_pad)

    var i: int = 0
    while (i < len(data)) {
        var ch = ord(slice(data, i, i + 1))
        var c = to_binary_table[band(ch, 127)]
        var padding = (ch == pad_code)
        // Skip illegal characters and whitespace
        if (c == -1) {
            i = i + 1
            continue
        }

        // Collect data into leftdata, update bitcount
        leftdata = bor(shl(leftdata, 6), c)
        leftbits = leftbits + 6

        // If we have 8 or more bits, append 8 bits to the result
        if (leftbits >= 8) {
            leftbits = leftbits - 8
            // Append if not padding
            if (padding == false) {
                result = result ++ chr(band(shr(leftdata, leftbits), 255))
            }
            leftdata = band(leftdata, shl(1, leftbits) - 1)
        }
        i = i + 1
    }

    return result
}

pn run() {
    // Build a random-ish string of 8192 lowercase letters
    // Use deterministic PRNG instead of Math.random()
    var state = {seed: 12345}
    var str = ""
    var i: int = 0
    while (i < 8192) {
        // Park-Miller PRNG
        var s = state.seed
        var hi: int = s / 127773
        var lo = s % 127773
        s = 16807 * lo - 2836 * hi
        if (s <= 0) {
            s = s + 2147483647
        }
        state.seed = s
        var r: int = s % 26
        str = str ++ chr(r + 97)
        i = i + 1
    }

    i = 8192
    while (i <= 16384) {
        var base64 = to_base64(str)
        var encoded = base64_to_string(base64)
        if (encoded != str) {
            print("base64: FAIL - decode mismatch at size " ++ string(i) ++ "\n")
            return false
        }
        // Double the string
        str = str ++ str
        i = i * 2
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
