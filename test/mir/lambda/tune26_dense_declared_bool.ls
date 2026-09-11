// Tune26 T26-2: a declared bool[] carries its boundary certificate into a
// counted loop, so the loop checks the carrier once and reads packed bytes.

pn tune26_dense_declared_bool(n: int) int {
    var flags: bool[] = fill(n, true)
    var count: int = 0
    var i: int = 0
    while (i < n) {
        if (flags[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}

pn main() {
    print(tune26_dense_declared_bool(8)); print("\n")
}
