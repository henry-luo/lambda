// Tune26 T26-5: non-null ordered IntLane pairs stay in signed order after
// excluding the nan lane whose position is intentionally non-numeric.

pn tune26_lt_pair(left: int, right: int) bool { return left < right }
pn tune26_le_pair(left: int, right: int) bool { return left <= right }
pn tune26_gt_pair(left: int, right: int) bool { return left > right }
pn tune26_ge_pair(left: int, right: int) bool { return left >= right }

pn main() {
    var zero: int = 0
    let nan_value: int = zero div zero
    let one: int = 1
    let negative_infinity: int = (-one) div zero
    let positive_infinity: int = one div zero
    print(tune26_lt_pair(nan_value, one)); print(" ")
    print(tune26_lt_pair(one, nan_value)); print(" ")
    print(tune26_lt_pair(negative_infinity, positive_infinity)); print("\n")
    print(tune26_le_pair(nan_value, nan_value)); print(" ")
    print(tune26_le_pair(one, nan_value)); print(" ")
    print(tune26_le_pair(positive_infinity, positive_infinity)); print("\n")
    print(tune26_gt_pair(one, nan_value)); print(" ")
    print(tune26_gt_pair(nan_value, one)); print(" ")
    print(tune26_gt_pair(positive_infinity, negative_infinity)); print("\n")
    print(tune26_ge_pair(nan_value, nan_value)); print(" ")
    print(tune26_ge_pair(nan_value, one)); print(" ")
    print(tune26_ge_pair(positive_infinity, positive_infinity)); print("\n")
}
