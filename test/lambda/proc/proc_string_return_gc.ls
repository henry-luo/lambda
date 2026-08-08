// Tune16 C0.B/D-c: a typed string-returning pn survives the GC work done by
// the nested concatenation builder before its result reaches the caller.

let TABLE = ["A", "B", "C", "D"]

pn enc(bytes, num_bytes) string {
    var result = ""
    var i = 0
    while (i + 2 < num_bytes) {
        var b0 = bytes[i]
        var b1 = bytes[i + 1]
        var b2 = bytes[i + 2]
        result = result ++ (TABLE[shr(b0, 2) % 4]
            ++ TABLE[b0 % 4]
            ++ TABLE[b1 % 4] ++ TABLE[b2 % 4])
        i = i + 3
    }
    return result
}

pn main() {
    var bytes = fill(30000, 97)
    print(len(enc(bytes, 30000)))
    print("|\n")
}
