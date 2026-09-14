// T20-3 #3 (D2.4.1-D2.4.3): the inline native shift/bitwise lowering is a
// MIR word instruction, so each operand opens its proven `int` lane. The
// registry row for shl/shr describes only the boxed fn_*_item fallback; when
// the emitter asked that descriptor for the inline operands it boxed them
// and shifted the tag bits (paraffins `nb(23) = inf`, mandelbrot2 `bor:
// operands must be integer`).

pn half(n: int) int {
    var h: int = shr(n, 1)
    return h
}

pn shift(a: int, b: int) int {
    return shl(a, b)
}

pn mask(a: int, b: int) int {
    return band(a, bnot(b))
}

pn main() {
    print(half(23))
    print(" ")
    print(shift(3, 4))
    print(" ")
    print(mask(255, 15))
    print("\n")
}
