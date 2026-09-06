// T21-2e: a parameter every typable caller passes a string to, and that the
// body only subscripts, takes the String* lane an annotated `s: string`
// would take: `s[i]` lowers to fn_string_ascii_at (one bounds check, one byte
// load) instead of an allocating fn_index. A caller the AST types as another
// concrete kind (an array literal) withholds the lane, and a caller nobody
// can type (a local call result) is routed by the wrapper's exact shape guard
// to the slow body, so the untyped semantics are unchanged on both tiers.
pn count_a(s) {
    var n = 0
    var i = 0
    while (i < len(s)) {
        if (s[i] == "a") { n = n + 1 }
        i = i + 1
    }
    return n
}
pn first(s) { return s[0] }
pn make(n) {
    var s = "a"
    var i = 1
    while (i < n) { s = s ++ "ba"; i = i + 1 }
    return s
}
pn main() {
    print(count_a("banana")); print(" "); print(count_a(make(4))); print(" ")
    print(first("xyz")); print(" "); print(first([7, 8])); print("\n")
}
