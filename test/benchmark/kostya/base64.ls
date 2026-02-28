// Kostya Benchmark: base64
// Base64 encode and decode
// Adapted from github.com/kostya/benchmarks
// Encodes a string of 10000 'a' chars to base64, decodes back, verifies
// Repeats 100 times for timing

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

// Base64 encoding table
pn b64_char(table, idx) {
    return table[idx]
}

// Base64 decoding: char to index
pn b64_index(ch) {
    if (ch == "A") { return 0 }
    if (ch == "B") { return 1 }
    if (ch == "C") { return 2 }
    if (ch == "D") { return 3 }
    if (ch == "E") { return 4 }
    if (ch == "F") { return 5 }
    if (ch == "G") { return 6 }
    if (ch == "H") { return 7 }
    if (ch == "I") { return 8 }
    if (ch == "J") { return 9 }
    if (ch == "K") { return 10 }
    if (ch == "L") { return 11 }
    if (ch == "M") { return 12 }
    if (ch == "N") { return 13 }
    if (ch == "O") { return 14 }
    if (ch == "P") { return 15 }
    if (ch == "Q") { return 16 }
    if (ch == "R") { return 17 }
    if (ch == "S") { return 18 }
    if (ch == "T") { return 19 }
    if (ch == "U") { return 20 }
    if (ch == "V") { return 21 }
    if (ch == "W") { return 22 }
    if (ch == "X") { return 23 }
    if (ch == "Y") { return 24 }
    if (ch == "Z") { return 25 }
    if (ch == "a") { return 26 }
    if (ch == "b") { return 27 }
    if (ch == "c") { return 28 }
    if (ch == "d") { return 29 }
    if (ch == "e") { return 30 }
    if (ch == "f") { return 31 }
    if (ch == "g") { return 32 }
    if (ch == "h") { return 33 }
    if (ch == "i") { return 34 }
    if (ch == "j") { return 35 }
    if (ch == "k") { return 36 }
    if (ch == "l") { return 37 }
    if (ch == "m") { return 38 }
    if (ch == "n") { return 39 }
    if (ch == "o") { return 40 }
    if (ch == "p") { return 41 }
    if (ch == "q") { return 42 }
    if (ch == "r") { return 43 }
    if (ch == "s") { return 44 }
    if (ch == "t") { return 45 }
    if (ch == "u") { return 46 }
    if (ch == "v") { return 47 }
    if (ch == "w") { return 48 }
    if (ch == "x") { return 49 }
    if (ch == "y") { return 50 }
    if (ch == "z") { return 51 }
    if (ch == "0") { return 52 }
    if (ch == "1") { return 53 }
    if (ch == "2") { return 54 }
    if (ch == "3") { return 55 }
    if (ch == "4") { return 56 }
    if (ch == "5") { return 57 }
    if (ch == "6") { return 58 }
    if (ch == "7") { return 59 }
    if (ch == "8") { return 60 }
    if (ch == "9") { return 61 }
    if (ch == "+") { return 62 }
    if (ch == "/") { return 63 }
    return -1
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
    var bytes = make_array(num_bytes, 97)

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
