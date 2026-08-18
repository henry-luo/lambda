// Negative test: conceptual type names are not Lambda annotation syntax.
// `int64` is prose/concept only; the defined sized-type name is `i64`.
// The diagnostic must name the unknown type, suggest the defined syntax,
// and must not cascade a follow-on E201 boundary message.
pn main() {
    var a: int64 = 1i64
    print(string(a))
}
