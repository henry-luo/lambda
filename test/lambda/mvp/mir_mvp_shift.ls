// S4.1.2: arithmetic shifts retain int53, poison and checked-count semantics.
pn typed(a: int, count: int) any { return shr(a, count) }
pn boxed(a: any, count: any) any { return shr(a, count) }
pn fixed(a: int) any { return shr(a, 1) }
pn divide(a: int, b: int) int { return a div b }
pn bad_count(count: int) {
    var result: int = shr(7, count)
    print("UNEXPECTED COUNT\n")
}
pn packed(a: int, count: int) any { return bor(shl(a, count), 1) }
pn nested(a: int, b: int) any { return bxor(bor(a, b), shl(a, 2)) }
pn mark(a: int) int { print(a); print(" "); return a }
pn effectful_bits() any { return bor(shl(mark(3), 1), mark(1)) }
pn main() {
    print(packed(3, 1)); print(" "); print(packed(-3, 1)); print(" "); print(packed(7, 64)); print("\n")
    print(packed(1, -1)); print(" "); print(packed(1, 53)); print(" "); print(packed(1, 63)); print("\n")
    print(packed(int(inf), 1)); print(" "); print(packed(int(nan), 1)); print("\n")
    print(nested(3, 8)); print(" "); print(effectful_bits()); print("\n")

    print(typed(42, 1)); print(" "); print(typed(-43, 1)); print("\n")
    print(typed(9007199254740991, 0)); print(" "); print(typed(-9007199254740991, 63)); print("\n")
    print(typed(7, 64)); print(" "); print(typed(-7, 64)); print(" "); print(typed(7, 9007199254740991)); print("\n")
    print(typed(-43, 1) == boxed(-43, 1)); print(" "); print(typed(42, 2) + 5); print("\n")
    let infinity: int = divide(1, 0)
    let poison: int = divide(0, 0)
    print(string(typed(infinity, 1)) == string(boxed(infinity, 1))); print(" ")
    print(string(typed(poison, 1)) == string(boxed(poison, 1))); print("\n")
    print(fixed(-43)); print(" "); print(string(fixed(infinity)) == string(boxed(infinity, 1))); print(" ")
    print(string(fixed(poison)) == string(boxed(poison, 1))); print("\n")
    bad_count(-1)
    print("checked shift count\n")
}
