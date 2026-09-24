// A `bool` system function returns an ordinary error for an error operand or
// an operand outside its domain (S7.9.3, S7.10.4); `bool` is only its success
// shape (D6.4.1). The JIT narrowed any/all/is_view results into the bool lane
// through truthiness, reading the error as `false`, and let the BOOL_ERROR of
// contains/starts_with/ends_with reach `if` and `not` as truth. The
// interpreter kept the error Item; the tiers must agree (S1.6).

fn dyn(v) => v
fn masks(m: bool[], s: string) { [any(m), all(m), is_view(m), starts_with(s, "a"), contains(s, "b")] }
// `^` on these calls is legal now that they may return error; it takes the
// boxed result -- a raw Bool lane there was read as an Item pointer
fn has_a(x) bool^ { contains(x, "a")^ }
fn some(x) bool^ { any(x)^ }

pn main() {
    let e = dyn([1, 4, 5]) > 2

    // an error operand returns the error; so does a non-sequence or non-text
    print([type(e), any(e), all(e), is_view(e), any(dyn(5)), all(dyn("x"))])
    print("\n")
    print([contains(e, "a"), starts_with(e, "a"), ends_with(e, "a"),
        contains(dyn(5), "a"), contains(dyn("ab"), dyn('b')), starts_with(dyn(5), "a")])
    print("\n")

    // the error is falsy where truthiness decides (S7.9.2), and stays an
    // error in a binding
    print([if (any(e)) "t" else "f", if (all(e)) "t" else "f",
        if (contains(e, "a")) "t" else "f", if (starts_with(e, "a")) "t" else "f"])
    print("\n")
    print([not any(e), not contains(e, "a"), any(e) or "rescued", ends_with(e, "a") or "rescued"])
    print("\n")
    let a = any(e)
    let c = contains(e, "a")
    let ok: bool = all(dyn([true, 1])) or false
    print([a, c, a is error, c is error, ok])
    print("\n")

    // valid operands still answer, through the boxed and the native lanes
    print([any(dyn([false, 0, "x"])), all(dyn([true, 1])), contains(dyn("abc"), "b"),
        starts_with(dyn('abc'), "ab"), ends_with(dyn("abc"), "bc"), any(null), all(1 to 3)])
    print("\n")
    print(masks([false, true], "abc"))
    print("\n")

    // propagation and handlers discharge the error channel
    print([has_a(dyn("abc")), has_a(dyn(5)) is error, some(dyn([0])), some(dyn(1)) is error,
        contains(dyn(5), "a") ^ { "handled" }, all(dyn(5)) ^ { "handled" } ~ { ~ }])
    print("\n")
}
