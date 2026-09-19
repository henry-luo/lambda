// Tune31 T31-1 / D3.2.1-D3.3.1: the initial fill establishes an int lane;
// recursive var forwarding must preserve that witness and its write-back.

pn recursive_fill(var values, n) {
    if (n <= 0) {
        return 0
    }
    values[n - 1] = n
    return recursive_fill(values, n - 1) + n
}

pn main() {
    var values = fill(4, 0)
    print(recursive_fill(values, 4) ++ " " ++ values[3] ++ "\n")
}
