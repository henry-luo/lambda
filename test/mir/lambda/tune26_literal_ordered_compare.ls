// Tune26 T26-5: a dynamic IntLane ordered against an int literal excludes
// only the sentinel whose signed order differs from numeric ordering.
//
// The `absent` column is `null`, not `false`: S6.1.2 makes an ordered relation
// with a null operand absorb to null, the same rule as `null + 1 -> null`. The
// golden carried `false` here until 2026-09-13 because the JIT folded the null
// sentinel into its bool answer while the interpreter had always returned null
// -- the golden matched only the JIT. Absence is falsy, so control flow reads
// the same either way; the VALUE is what differed.

pn tune26_lt_literal(value: int?) bool { return value < 2 }
pn tune26_literal_lt(value: int?) bool { return 2 < value }
pn tune26_gt_literal(value: int?) bool { return value > 2 }
pn tune26_literal_gt(value: int?) bool { return 2 > value }

pn main() {
    var zero: int = 0
    let nan_value: int = zero div zero
    let one: int = 1
    let negative_infinity: int = (-one) div zero
    let positive_infinity: int = one div zero
    let absent: int? = null
    print(tune26_lt_literal(1)); print(" ")
    print(tune26_lt_literal(nan_value)); print(" ")
    print(tune26_lt_literal(negative_infinity)); print(" ")
    print(tune26_lt_literal(positive_infinity)); print(" ")
    print(tune26_lt_literal(absent)); print("\n")
    print(tune26_literal_lt(1)); print(" ")
    print(tune26_literal_lt(nan_value)); print(" ")
    print(tune26_literal_lt(negative_infinity)); print(" ")
    print(tune26_literal_lt(positive_infinity)); print(" ")
    print(tune26_literal_lt(absent)); print("\n")
    print(tune26_gt_literal(3)); print(" ")
    print(tune26_gt_literal(nan_value)); print(" ")
    print(tune26_gt_literal(negative_infinity)); print(" ")
    print(tune26_gt_literal(positive_infinity)); print(" ")
    print(tune26_gt_literal(absent)); print("\n")
    print(tune26_literal_gt(1)); print(" ")
    print(tune26_literal_gt(nan_value)); print(" ")
    print(tune26_literal_gt(negative_infinity)); print(" ")
    print(tune26_literal_gt(positive_infinity)); print(" ")
    print(tune26_literal_gt(absent)); print("\n")
}
