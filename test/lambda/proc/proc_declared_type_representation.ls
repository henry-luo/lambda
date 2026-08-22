// Regression coverage: a declared type must keep describing the value's runtime
// representation. Both halves of this file crashed or miscompiled before the
// 2026-07-29 fixes in transpile-mir.cpp, and neither had any test.

pn show(label, value) {
    print(label)
    print(":")
    print(value)
    print("\n")
}

// ---------------------------------------------------------------------------
// A. `string` return type on a pn.
// infer_return_type() refuses string as a native return, so the callee hands
// back a boxed Item while the call expression's static type says STRING. The
// String*-taking builtins (len/ord/starts_with/ends_with) took that at face
// value and dereferenced a tagged Item — a hard SIGSEGV on any `len(f())`,
// at any string length.
// ---------------------------------------------------------------------------
pn make_str() string {
    return "ABC"
}

pn build_str(n: int) string {
    var r = ""
    var i: int = 0
    while (i < n) {
        r = r ++ "a"
        i = i + 1
    }
    return r
}

// ---------------------------------------------------------------------------
// B. declared int/i64/float bound from an `integer`-carrier expression.
// len() is i64 but ordinary arithmetic widens to the semantic `integer`
// carrier, which is LMD_TYPE_DECIMAL at runtime. With no coercion the decimal
// pointer Item was stored into a register later read as a native lane: the
// binding read back as <error>, and as a loop counter it never terminated.
// ---------------------------------------------------------------------------
pn pad_count(s: string) int {
    var pad: int = 9 - len(s)
    return pad
}

pn pad_string(s: string) {
    var pad: int = 9 - len(s)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    return prefix ++ s
}

pn main() {
    // A — every String*-taking builtin against a declared-string return
    show("len", len(make_str()))
    show("ord", ord(make_str()))
    show("starts_with", starts_with(make_str(), "A"))
    show("ends_with", ends_with(make_str(), "C"))
    show("concat", make_str() ++ "!")
    show("slice", slice(make_str(), 1, 3))
    // length must not matter: the original repro only looked size-dependent
    show("len_short", len(build_str(3)))
    show("len_long", len(build_str(500)))

    // B — declared native type from a decimal-carrier expression
    show("pad_count", pad_count("1234"))
    show("pad_string", pad_string("1234"))
    var direct: int = int(5n)
    show("int_from_integer_literal", direct)
    var as_i64 = i64(9 - len("abcd"))
    show("int64_from_len_expr", as_i64)
    var as_float: float = float(9n)
    show("float_from_integer", as_float)
    // the same binding in a let, and inside fn, took the identical bad path
    let in_let: int = int(9 - len("abcd"))
    show("let_int_from_len_expr", in_let)
}
