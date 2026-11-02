// S4.1.2, S4.5.2, S7.7, S9.1.2: guarded regions preserve exceptional entries and snapshots.
pn sequence(n: int, limit: int) int {
    var steps: int = 0
    while (steps < limit) {
        if (n % 2 == 0) { n = shr(n, 1) } else { n = 3 * n + 1 }
        steps = steps + 1
    }
    return n
}
pn sequential(n: int, limit: int) int {
    var i: int = 0
    while (i < limit) { n = n * 3; n = n + 1; i = i + 1 }
    return n
}
pn separate_branches(n: int) int {
    var i: int = 0
    while (i < 2) {
        if (n > 0) { n = n * 3 }
        if (n > 0) { n = n + 1 }
        i = i + 1
    }
    return n
}
pn pack(var a: int[], lo: int, hi: int) int {
    var i: int = lo
    var j: int = lo
    while (j < hi) {
        if (a[j] > 0) { a[i] = a[j]; i = i + 1 }
        j = j + 1
    }
    a[i] = 0
    return i
}
pn pack_from(var a: int[], i: int, j: int, hi: int) int {
    while (j < hi) {
        a[i] = a[j]
        i = i + 1
        j = j + 1
    }
    return i
}
pn after_step(var a: int[], hi: int) int {
    var i: int = 0
    var j: int = 0
    while (j < hi) { i = i + 1; a[i] = a[j]; j = j + 1 }
    return i
}
pn fraction(i: int, j: int) float { return (i + j) * (i + j + 1) / 2 + i + 1 }
pn rounded(n: int) float { return n / 2 + n + 1 }
pn generic_round(n: any) any { return n / 2 + n + 1 }
pn zero(n: int) float { return n / 2 - n / 2 }
pn identity(x: float) float { return x }
pn fixed(x: float) float { return x + x }
pn changed(x: float) float { x = x + 1.0; return x }
pn same_int(x: int) int { return x + x }
pn two_int(x: int) int { return x }
pn write_int(x: int) int { x = x + 1; return x }
pn divide(a: int, b: int) int { return a div b }
pn grow(n: int, limit: int) int {
    var i: int = 0
    while (i < limit) { n = n * 3 + 1; i = i + 1 }
    return n
}
pn repeated_range() bool {
    var sum: int = 9007199254740988
    var i: int = 0
    while (i < 2) { for j in 1 to 2 { sum = sum + 1 }; i = i + 1 }
    return sum - 1 > 9007199254740991
}
pn checked_read(a: int[], k: int) int { var x: int = a[k]; return x }
pn float_count(var a: int[], hi: int) float {
    var s: float = 0.5
    var j: int = 0
    while (j < hi) { a[j] = 1; s = s + 1; j = j + 1 }
    return s
}
pn main() {
    print(sequence(10, 4)); print(" "); print(sequence(-10, 4)); print(" ")
    print(sequence(10, 72)); print("\n")
    print(sequence(3002399751580331, 1) > 9007199254740991); print(" ")
    var poison: int = divide(0, 0)
    let poisoned = sequence(poison, 1)
    print(poisoned != poisoned); print("\n")
    print(sequential(2, 3)); print(" "); print(separate_branches(2)); print("\n")
    var a: int[] = [3, -1, 2, 0, 5]
    print(pack(a, 0, 4)); print(" "); print(a[0]); print(" "); print(a[1]); print(" "); print(a[2]); print("\n")
    var b: int[] = [3, -1, 2, 0, 5]
    let old: int[] = b
    print(pack(b, 0, 4)); print(" "); print(old[1]); print(" "); print(b[1]); print("\n")
    print(pack(b, 4, 2)); print(" "); print(b[4]); print("\n")
    print(pack(b, 0, 8)); print(" "); print(b[2]); print("\n")
    var c: int[] = [3, 4, 5, 6]
    print(pack_from(c, 2, 0, 2)); print(" "); print(c[2]); print(" "); print(c[3]); print("\n")
    print(after_step(c, 2)); print(" "); print(c[1]); print(" "); print(c[2]); print("\n")
    print(fraction(0, 0)); print(" "); print(fraction(2, 3)); print(" "); print(fraction(-3, 1)); print("\n")
    print(rounded(9007199254740991) == generic_round(9007199254740991)); print(" ")
    print(1.0 / zero(0) > 0.0); print("\n")
    print(1.0 / identity(0.0) > 0.0); print(" "); print(1.0 / identity(-0.0) < 0.0); print(" ")
    print(fixed(1.5)); print(" "); print(changed(1.5)); print("\n")
    print(same_int(3)); print(" "); print(two_int(1)); print(" "); print(two_int(2)); print(" "); print(write_int(2)); print("\n")
    print(grow(100000000000000, 5) > 9007199254740991); print(" "); print(repeated_range()); print("\n")
    if (true) { var x: int = checked_read(a, -1); print("UNEXPECTED CALL TAIL\n") }
    print("checked call boundary\n")
    print(float_count(a, 2)); print("\n")
}
