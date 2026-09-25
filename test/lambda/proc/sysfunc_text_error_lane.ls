// LR07-18 (S7.9.3, S7.10.2, S11.4.9, S1.6): a system function's error result
// stays an error on every tier; its registry row declares the error effect. `string`/`symbol`/`float`/`int` is only the
// success shape (D6.4.1). The JIT unboxed such results into a native lane:
// a string became "<error>", a symbol null (or a LambdaError read as a
// Symbol), a float nan and an int 0; interp kept the error Item.

fn cut(s: string, i: int) string | error => slice(s, i, 3)

pn main() {
    let e = error("boom")

    // invalid-but-present input (S7.10.2): non-integral offsets and counts
    print([slice("hello", 1.5, 3) is error, slice("hello", 1.5) is error,
        take("hello", 1.5) is error, drop("hello", 1.5) is error,
        slice('hello', 1.5, 3) is error, take('hello', 1.5) is error])
    print("\n")
    // non-text operands, non-complex operands
    print([replace("abc", 1, "x") is error, replace('abc', "a", 1) is error,
        url_resolve(1, 2) is error, symbol("a", 1) is error,
        real(1.5) is error, imag("abc") is error])
    print("\n")
    // an error operand is returned (S7.9.3), whichever argument it is
    print([chr(e) is error, ndim(e) is error, sort("cab", e) is error,
        drop("hello", e) is error, symbol("a", e) is error,
        slice('hello', e, 3) is error])
    print("\n")
    // `int` admits nan/inf, so an int-typed offset is no totality proof
    print([cut("hello", 0 / 0) is error, cut("hello", 1)])
    print("\n")
    // the error is falsy where truthiness decides (S7.9.2) and propagates
    // through consumers instead of reading as a 7-character string
    print([slice("hello", 1.5, 3) or "dflt", string(slice("hello", 0, 2))])
    print("\n")
    // proven-clean sites keep their values
    print([slice("hello", 1, 3), take('abc', 2), drop("hello", 3),
        replace("abc", "b", "x"), chr(65), ndim([1, 2])])
    print("\n")
}
