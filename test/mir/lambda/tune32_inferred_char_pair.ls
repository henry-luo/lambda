// D8.3.2 / S7.1.1v3: inferred fusion agrees with materialized reads.
pn tune32_same_at(left, li: int, right, ri: int) bool {
    return left[li] == right[ri]
}
pn tune32_different_at(left, li: int, right, ri: int) bool {
    return left[li] != right[ri]
}
pn tune32_materialized(left, li: int, right, ri: int) bool {
    var a = left[li]
    var b = right[ri]
    return a == b
}
pn tune32_open_at(left, key, right, other_key) {
    return left[key] == right[other_key]
}
pn tune32_check(left: string, li: int, right: string, ri: int) {
    let equal = tune32_same_at(left, li, right, ri)
    print(equal == tune32_materialized(left, li, right, ri))
    print(" "); print(tune32_different_at(left, li, right, ri) == not equal); print("\n")
}
pn tune32_nullable(left: string?, right: string?) bool {
    return left[0] == right[0]
}
pn tune32_text(text: string, var calls: int[]) string { calls[0] = calls[0] + 1; return text ++ "" }
pn tune32_key(key: int, var calls: int[]) int { calls[0] = calls[0] + 1; return key }
pn tune32_computed() {
    var calls: int[] = [0]
    var equal = tune32_text("é", calls)[tune32_key(0, calls)] == tune32_text("é", calls)[tune32_key(0, calls)]
    print(equal); print(" "); print(calls[0]); print("\n")
}
pn tune32_rebound(text) {
    var i = 0
    while (i < 2) {
        print(text[0] == text[0])
        if (i == 0) { print(" ") } else { print("\n") }
        text = [1, 2]
        i = i + 1
    }
}
pn tune32_clear_text(var text) { text = null }
pn tune32_borrowed_text(text) bool {
    tune32_clear_text(text)
    return text[0] == text[0]
}
pn tune32_captured_text(text) bool {
    fn read_snapshot(dummy: int) bool => text[0] == "ab"[0]
    let reader = read_snapshot
    text = null
    return reader(0)
}
pn tune32_failed_text(var calls: int[]) string^ {
    calls[0] = calls[0] + 1
    raise error("stopped")
}
pn tune32_failed_argument(var calls: int[]) bool^ {
    return tune32_same_at(tune32_failed_text(calls)^, 0, "a", 0)
}
pn main() {
    tune32_check("ab", 1, "xb", 1)
    tune32_check("ab", 0, "xb", 0)
    tune32_check("é", 0, "e", 0)
    tune32_check("😀", 0, "😀", 0)
    tune32_check("", 0, "x", 0)
    tune32_check("", 0, "", 0)
    tune32_check("ab", -1, "ab", 2)
    tune32_check("ab", 99, "ab", 0)
    print(tune32_nullable(null, null)); print(" "); print(tune32_nullable(null, "x")); print("\n")
    print(tune32_open_at("ab", 0, "ab", 0)); print(" ")
    print(tune32_open_at("ab", "bad", "ab", "bad")); print(" ")
    print(tune32_open_at([7], 0, [8], 0)); print("\n")
    tune32_computed()
    tune32_rebound("ab")
    print(tune32_borrowed_text("ab")); print("\n")
    print(tune32_captured_text("ab")); print(" ")
    print(tune32_captured_text(null)); print("\n")
    var calls: int[] = [0]
    var handled = false
    tune32_failed_argument(calls) ^ { handled = true }
    print(handled); print(" "); print(calls[0]); print("\n")
}
