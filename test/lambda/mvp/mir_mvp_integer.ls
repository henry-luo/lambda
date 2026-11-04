// S4.1.2, D7.4.1: guarded loops and bitwise operations retain their checked miss paths.
pn countdown(n: int, seed: int) int {
    var total: int = seed
    while (n >= 0) {
        total = total + n
        n = n - 1
    }
    return total
}
pn reversed(n: int) int {
    var total: int = 0
    while (n >= 0) {
        n = n - 1
        total = total + n
    }
    return total
}
pn altered(n: int) int {
    var total: int = 0
    while (n >= 0) {
        total = total + n
        n = n - 1
        n = n - 1
    }
    return total
}
pn shift(a: int, b: int) any { return shl(a, b) }
pn boxed_shift(a: any, b: any) any { return shl(a, b) }
pn mask(a: int, b: int) any { return bxor(a, b) }
pn bounded() float {
    let end: int = 12
    var i: int = 0
    var total: float = 0.0
    while (i < end) { total = total + i; i = i + 1 }
    return total
}
pn reset(limit: int) int {
    var i: int = 0
    while (i < limit) { i = 9007199254740991; i = i + 1; return i }
    return i
}
pn nested() int {
    var i: int = 0
    while (i < 3) {
        var j: int = 0
        while (j < 10) { i = i + 1; j = j + 1 }
    }
    return i
}
pn main() {
    print(countdown(0, 0)); print("\n")
    print(countdown(17, 0)); print("\n")
    print(countdown(-1, 8)); print("\n")
    print(countdown(3, 2)); print("\n")
    print(countdown(2, 9007199254740991)); print("\n")
    print(reversed(3)); print("\n")
    print(altered(4)); print("\n")
    print(mask(255, 15)); print("\n")
    print(mask(-9007199254740991, 1)); print("\n")
    print(shift(3, 4)); print("\n")
    print(shift(1, 53)); print("\n")
    print(shift(-1, 53)); print("\n")
    print(shift(1, 63) == boxed_shift(1, 63)); print("\n")
    print(shift(1, 64) == boxed_shift(1, 64)); print("\n")
    print(shift(1, -1)); print("\n")
    print(boxed_shift("bad", 1)); print("\n")
    print(bounded()); print("\n")
    print(reset(1)); print("\n")
    print(nested()); print("\n")
    let saturated = countdown(1, 9007199254740991)
    print(countdown(saturated - saturated, 9)); print("\n")
    print(countdown(-saturated, 10)); print("\n")
}
