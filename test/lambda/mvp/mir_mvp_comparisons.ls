// S5.1, S6.1.2v2: nullable numeric equality differs from ordered comparison.
pn compare(a: int[], b: float[], i: int, j: int) {
    print(a[i] == b[j]); print(" "); print(a[i] != b[j]); print(" ")
    print(a[i] < b[j]); print(" "); print(a[i] <= b[j]); print(" ")
    print(a[i] > b[j]); print(" "); print(a[i] >= b[j]); print("\n")
}
pn compare_int(a: int[], i: int, j: int) {
    print(a[i] == a[j]); print(" "); print(a[i] != a[j]); print(" ")
    print(a[i] < a[j]); print(" "); print(a[i] <= a[j]); print(" ")
    print(a[i] > a[j]); print(" "); print(a[i] >= a[j]); print("\n")
}
pn divide(a: int, b: int) int { return a div b }
// S4.2.1: NaN is unordered on either side, including direct branch demand.
pn comparison_mask(a: int, b: int) int {
    var mask: int = 0
    if (a == b) { mask = mask + 1 }
    if (a != b) { mask = mask + 2 }
    if (a < b) { mask = mask + 4 }
    if (a <= b) { mask = mask + 8 }
    if (a > b) { mask = mask + 16 }
    if (a >= b) { mask = mask + 32 }
    return mask
}
pn zero_mask(a: int) int {
    var mask: int = 0
    if (a < 0) { mask = mask + 1 }
    if (a <= 0) { mask = mask + 2 }
    if (a > 0) { mask = mask + 4 }
    if (a >= 0) { mask = mask + 8 }
    if (0 < a) { mask = mask + 16 }
    if (0 <= a) { mask = mask + 32 }
    if (0 > a) { mask = mask + 64 }
    if (0 >= a) { mask = mask + 128 }
    return mask
}
pn loop_compare(a: int, b: int) int {
    var ordered: int = 0
    var unequal: int = 0
    while (ordered < 1 and a <= b) { ordered = ordered + 1 }
    while (unequal < 1 and a != b) { unequal = unequal + 1 }
    return ordered * 10 + unequal
}
pn main() {
    var a: int[] = [1, 2, divide(0, 0), divide(1, 0)]
    var b: float[] = [1.0, 2.0, math.sqrt(-1.0)]
    compare(a, b, 0, 0)
    compare(a, b, 0, 1)
    compare(a, b, 1, 0)
    compare(a, b, 0, 3)
    compare(a, b, 4, 0)
    compare(a, b, 4, 3)
    compare(a, b, 2, 0)
    compare(a, b, 0, 2)
    compare(a, b, 2, 2)
    compare(a, b, 3, 0)
    compare_int(a, 0, 1)
    compare_int(a, 2, 2)
    compare_int(a, 4, 4)
    compare_int(a, 0, 4)
    if (a[4] <= a[0]) { print("UNEXPECTED NULL BRANCH\n") }
    for i in 0 to 3 {
        for j in 0 to 3 {
            if (j > 0) { print(" ") }
            print(comparison_mask(a[i], a[j]))
        }
        print("\n")
    }
    print(zero_mask(a[0])); print(" "); print(zero_mask(a[2])); print(" ")
    print(zero_mask(a[3])); print(" "); print(zero_mask(divide(-1, 0))); print(" ")
    print(zero_mask(0)); print("\n")
    print(loop_compare(a[0], a[1])); print(" "); print(loop_compare(a[1], a[0])); print(" ")
    print(loop_compare(a[2], a[0])); print(" "); print(loop_compare(a[0], a[2])); print(" ")
    print(loop_compare(a[2], a[2])); print(" "); print(loop_compare(a[3], a[3])); print("\n")
}
