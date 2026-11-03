// S12.1.2v3, S2.5.3v3: fn-local control preserves explicit exits and fallthrough values.
fn choose(n: int) int {
    var value: int = n
    if (value < 0) { return 0 }
    while (value > 3) { value = value - 1 }
    return value
}
fn identity(n: int) int { return n }
fn collect(n: int) int {
    var value: int = n
    value
    if (value < 3) { value = value + 1 }
    while (value < 4) { value = value + 1 }
}
fn checked(value) int {
    let admitted: int = value
    return admitted
}
pn main() {
    print(choose(-1)); print("\n")
    print(choose(5)); print("\n")
    print(identity(7)); print("\n")
    print(collect(2)); print("\n")
    print(checked(9)); print("\n")
    let failed = checked("bad")
    if (failed) { print("unexpected admitted value\n") }
    print("after boundary\n")
}
