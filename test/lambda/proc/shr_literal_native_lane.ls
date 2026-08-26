// T20-3 #3. `shr` with a LITERAL in-range count has neither of the cold arms
// its boxing exists for: the count cannot be negative (no error Item) and a
// right shift cannot leave int53 (no overflow Item, unlike shl). The
// classifier veto, the carrier oracle and the lowering all consult
// mir_shr_native_literal_count, so `bxor(h, shr(h, 16))` lowers to two native
// instructions instead of two boxed helper calls. These pin the lane against
// the interpreter: signed behavior on negatives, count 0, chained bitwise
// consumers, and a dynamic count that must stay on the boxed helper.
pn hash(key: int) int {
    var h = key
    h = bxor(h, shr(h, 16))
    h = h * 73244475
    h = bxor(h, shr(h, 16))
    if (h < 0) { h = 0 - h }
    return h
}
pn main() {
    print(hash(12345)) print(" ")
    print(hash(0)) print(" ")
    print(hash(0 - 999983))
    print("\n")
    print(shr(1024, 3)) print(" ")
    print(shr(0 - 64, 2)) print(" ")
    print(shr(7, 0)) print(" ")
    print(shr(1, 63))
    print("\n")
    // dynamic count stays on the guarded boxed helper, including its error arm
    var n = 2
    print(shr(1024, n)) print(" ")
    print(shr(1024, n + 62))
    print("\n")
}
