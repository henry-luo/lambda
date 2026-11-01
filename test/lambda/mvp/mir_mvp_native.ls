// S4.1, S3.4, S7.9.3: native consumers retain poison, short-circuiting and errors.
pn shuffle(a: int, b: int, n: int) int {
    var saved: int = a + 1
    if (n <= 0) { return saved + b }
    return shuffle(b, a, n - 1) + saved
}
pn helper(x: float) float { if (x < 0.0) { return -x }; return x + 0.5 }
pn nested(x: float) float { return helper(x) + helper(-x) }
fn signed_square(x: float) float => if (x < 0.0) -(x * x) else x * x
pn observe(value: int) int { print("observed\n"); return value }
pn shifted_add(a: int, count: int, b: int) any { return shl(a, count) + b }
pn rejected_add() int {
    let result: int = shl(1, -1) + observe(2)
    print("unexpected rejected tail\n")
    return result
}
pn countdown(n: int) int {
    var total: int = 0
    while (n >= 0) { total = total + n; n = n - 1 }
    return total + n
}
pn loops() int {
    var i: int = 0
    var total: int = 0
    while (i < 9 and (i < 3 or i < 7)) {
        i = i + 1
        if (i == 2 or i == 4) { continue }
        total = total + i
        if (i == 6) { break }
    }
    while (false and observe(1) == 1) { total = 1000 }
    if (true or observe(2) == 2) { total = total + 1 }
    while (0) { total = total + 1; break }
    return total
}
pn main() {
    print(shuffle(2, 5, 2)); print("\n")
    print(nested(-2.0)); print("\n")
    print(signed_square(-3.0)); print("\n")
    print(signed_square(3.0)); print("\n")
    print(1.0 / 0.0); print("\n")
    print(1.0 / -0.0); print("\n")
    print(shifted_add(3, 2, 1)); print("\n")
    print(shifted_add(1, -1, 2)); print("\n")
    print(shifted_add(1, 53, 2)); print("\n")
    print((shl(3, 2) + 1) * (shl(2, 1) - 1)); print("\n")
    print(2 + shl(1, -1)); print("\n")
    print(shl(1, -1) == 2); print("\n")
    print(shl(1, -1) + shl(2, -1)); print("\n")
    print(shl(1, 1) + 0.5); print("\n")
    print(rejected_add()); print("\n")
    print(loops()); print("\n")
    print(countdown(0)); print("\n")
    print(countdown(1)); print("\n")
    print(countdown(2)); print("\n")
    print(countdown(3)); print("\n")
    print(countdown(4)); print("\n")
    print(countdown(5)); print("\n")
}
