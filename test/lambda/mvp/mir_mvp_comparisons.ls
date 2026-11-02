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
}
