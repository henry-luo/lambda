// Tune26 T26-3: finite integer parity uses one raw low-bit check while the
// sentinel arm retains the normal remainder and numeric comparison behavior.

pn tune26_is_even(n: int) bool {
    return n % 2 == 0
}

pn main() {
    var zero: int = 0
    let infinity: int = 1 div zero
    print(tune26_is_even(4)); print(" ")
    print(tune26_is_even(-3)); print(" ")
    print(tune26_is_even(-2)); print(" ")
    print(tune26_is_even(infinity)); print("\n")
}
