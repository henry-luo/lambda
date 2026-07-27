// Regression coverage for Tune10's guarded compact-int loop lane.
pn count_down(x, y) {
    var count = 0
    while (x >= y) {
        x = x - y
        count = count + 1
    }
    return count
}

pn remainder_down(x, y) {
    while (x >= y) {
        x = x - y
    }
    return x
}

pn main() {
    print("P:")
    print(count_down(9, 2))
    print(":")
    print(remainder_down(9, 2))

    // The non-positive lane must retain the generic lowering. This input exits
    // before the loop and proves it does not assume a positive step globally.
    print(" N:")
    print(count_down(-5, -2))
    print(":")
    print(remainder_down(-5, -2))
}
