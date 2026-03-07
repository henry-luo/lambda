// Kostya Benchmark: base64
// Base64 encode and decode
// Adapted from github.com/kostya/benchmarks
// Encodes a string of 10000 'a' chars to base64, decodes back, verifies
// Repeats 100 times for timing

// Base64 encoding table (array of single-char strings for fast indexed lookup)
let TABLE = ["A","B","C","D","E","F","G","H","I","J","K","L","M",
             "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
             "a","b","c","d","e","f","g","h","i","j","k","l","m",
             "n","o","p","q","r","s","t","u","v","w","x","y","z",
             "0","1","2","3","4","5","6","7","8","9","+","/"]

// Encode using integer array of byte values
pn b64_encode(bytes, num_bytes) {
    var result = ""
    var i = 0
    while (i + 2 < num_bytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        var b2 = bytes[i + 2]
        result = result ++ (TABLE[shr(b0, 2)]
            ++ TABLE[(b0 % 4) * 16 + shr(b1, 4)]
            ++ TABLE[(b1 % 16) * 4 + shr(b2, 6)] ++ TABLE[b2 % 64])
        i = i + 3
    }
    if (i + 1 == num_bytes) {
        var b0 = bytes[i]
        result = result ++ (TABLE[shr(b0, 2)]
            ++ TABLE[(b0 % 4) * 16] ++ "==")
    }
    if (i + 2 == num_bytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        result = result ++ (TABLE[shr(b0, 2)]
            ++ TABLE[(b0 % 4) * 16 + shr(b1, 4)]
            ++ TABLE[(b1 % 16) * 4] ++ "=")
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
    var __t0 = clock()
    // Create input: 10000 bytes all = 97 ('a')
    let num_bytes = 10000
    var bytes = fill(num_bytes, 97)

    var encoded = ""
    var decoded_len = 0

    var iter = 0
    while (iter < 100) {
        encoded = b64_encode(bytes, num_bytes)
        decoded_len = b64_decode_len(encoded)
        iter = iter + 1
    }
    var __t1 = clock()

    let enc_len = len(encoded)
    print("base64: encoded_len=" ++ string(enc_len) ++ " decoded_len=" ++ string(decoded_len) ++ "\n")
    if (decoded_len == num_bytes) {
        print("base64: PASS\n")
    } else {
        print("base64: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
