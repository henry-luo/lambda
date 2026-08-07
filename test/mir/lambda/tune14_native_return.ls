// Tune14 A3: a closed recursive procedure may return a proven native result,
// but a mutable accumulator must keep the boxed fallback until mutation-aware
// lane tracking exists.

pn tune14_recursive(n: int) int {
    if (n < 2) {
        return n
    }
    return tune14_recursive(n - 1) + tune14_recursive(n - 2)
}

pn tune14_mutable(n: int) int {
    var value: int = n
    if (n <= 0) {
        return value
    }
    value = value + tune14_mutable(n - 1)
    return value
}

pn main() {
    print(string(tune14_recursive(6)) ++ "\n")
    print(string(tune14_mutable(6)) ++ "\n")
}
