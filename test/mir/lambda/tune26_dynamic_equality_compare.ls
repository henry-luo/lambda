// Tune26 T26-5: a non-null IntLane equality must keep numeric nan unequal to
// itself while preserving ordinary finite and infinity equality.

pn tune26_eq_pair(left: int, right: int) bool { return left == right }
pn tune26_ne_pair(left: int, right: int) bool { return left != right }

pn main() {
    var zero: int = 0
    let nan_value: int = zero div zero
    let one: int = 1
    let positive_infinity: int = one div zero
    print(tune26_eq_pair(nan_value, nan_value)); print(" ")
    print(tune26_eq_pair(nan_value, one)); print(" ")
    print(tune26_eq_pair(positive_infinity, positive_infinity)); print("\n")
    print(tune26_ne_pair(nan_value, nan_value)); print(" ")
    print(tune26_ne_pair(nan_value, one)); print(" ")
    print(tune26_ne_pair(positive_infinity, positive_infinity)); print("\n")
}
