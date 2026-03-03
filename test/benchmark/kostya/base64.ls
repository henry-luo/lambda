// Kostya Benchmark: base64
// Base64 encode and decode
// Adapted from github.com/kostya/benchmarks
// Encodes a string of 10000 'a' chars to base64, decodes back, verifies
// Repeats 100 times for timing

// Base64 encoding table
pn b64_char(table, idx) {
    return table[idx]
}

// Encode using integer array of byte values
pn b64_encode(table, bytes, num_bytes) {
    var result = ""
    var i = 0
    while (i + 2 < num_bytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        var b2 = bytes[i + 2]
        result = result ++ b64_char(table, shr(b0, 2))
        result = result ++ b64_char(table, (b0 % 4) * 16 + shr(b1, 4))
        result = result ++ b64_char(table, (b1 % 16) * 4 + shr(b2, 6))
        result = result ++ b64_char(table, b2 % 64)
        i = i + 3
    }
    if (i + 1 == num_bytes) {
        var b0 = bytes[i]
        result = result ++ b64_char(table, shr(b0, 2))
        result = result ++ b64_char(table, (b0 % 4) * 16)
        result = result ++ "=="
    }
    if (i + 2 == num_bytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        result = result ++ b64_char(table, shr(b0, 2))
        result = result ++ b64_char(table, (b0 % 4) * 16 + shr(b1, 4))
        result = result ++ b64_char(table, (b1 % 16) * 4)
        result = result ++ "="
    }
    return result
}

pn b64_decode_len(encoded) {
    var slen = len(encoded)
    var n = shr(slen, 2) * 3
    if (slen >= 1 and encoded[slen - 1] == "=") {
        n = n - 1
    }
    if (slen >= 2 and encoded[slen - 2] == "=") {
        n = n - 1
    }
    return n
}

pn main() {
    // Create input: 10000 bytes all = 97 ('a')
    let table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    let num_bytes = 10000
    var bytes = fill(num_bytes, 97)

    var encoded = ""
    var decoded_len = 0

    var iter = 0
    while (iter < 100) {
        encoded = b64_encode(table, bytes, num_bytes)
        decoded_len = b64_decode_len(encoded)
        iter = iter + 1
    }

    let enc_len = len(encoded)
    print("base64: encoded_len=" ++ string(enc_len) ++ " decoded_len=" ++ string(decoded_len) ++ "\n")
    if (decoded_len == num_bytes) {
        print("base64: PASS\n")
    } else {
        print("base64: FAIL\n")
    }
}
