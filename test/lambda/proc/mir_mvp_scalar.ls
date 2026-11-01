// S4.1.2, S4.3: checked int arithmetic, IEEE comparisons and signed zero.
pn add(a: int, b: int) int { return a + b }
pn multiply(a: int, b: int) int { return a * b }
pn subtract(a: int, b: int) int { return a - b }
pn divide(a: int, b: int) int { return a div b }
pn remainder(a: int, b: int) int { return a % b }
pn mixed(a: int, b: float) float { return a + b }
pn loop(n: int) int {
    var sum: int = 0
    while (n > 0) {
        n = n - 1
        if (n == 4) { continue }
        if (n == 1) { break }
        sum = sum + n
    }
    return sum
}
pn main() {
    print(add(9007199254740991, 1))
    print("\n")
    print(multiply(4503599627370496, 262144))
    print("\n")
    let positive = add(9007199254740991, 1)
    let negative = subtract(-9007199254740991, 1)
    let indeterminate = add(positive, negative)
    print(indeterminate)
    print("\n")
    print(indeterminate == indeterminate)
    print("\n")
    print(indeterminate != indeterminate)
    print("\n")
    print(multiply(positive, 0))
    print("\n")
    print(divide(-7, 3))
    print("\n")
    print(remainder(-7, 3))
    print("\n")
    print(mixed(7, 0.5))
    print("\n")
    print(1.0 / -0.0)
    print("\n")
    print(loop(7))
    print("\n")
}
