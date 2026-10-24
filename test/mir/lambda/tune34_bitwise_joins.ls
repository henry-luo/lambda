// D8.3.3/S4.1: admit complete compact-int tuples; sized and error joins retain their domains.
pn words(values, count) {
    let a = values[0]
    let b = values[1]
    return [band(a, b), bor(a, b), bxor(a, b), shl(a, count), shr(a, count)]
}
pn main() {
    let indirect = words
    print(indirect([13, 6], 2)); print("\n")
    print(indirect([-13, 6], 1)); print("\n")
    print(indirect([13, 6], 64)); print("\n")
    print(type(indirect([13, 6], -1)[3]) == error); print("\n")
    print(indirect([u32(4294967295), u32(7)], 1)); print("\n")
    print(type(indirect([null, 1], 1)[0]) == error); print("\n")
    print(type(indirect([error("operand"), 1], 1)[2]) == error); print("\n")
    print(indirect([9007199254740991, -1], 1)); print("\n")
    print(indirect([1, 0], 63)); print("\n")
}
