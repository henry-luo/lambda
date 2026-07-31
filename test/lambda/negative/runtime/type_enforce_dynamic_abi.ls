pn main() {
    let captured = 0
    let sum8_closure = (a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) => captured + a + b + c + d + e + f + g + h
    sum8_closure(1, 2, 3, 4, 5, 6, 7, 8)
}
