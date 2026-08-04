// Core Lambda dynamic dispatch accepts the full 16 boxed argument ABI.

pn main() {
    let captured = 0
    let sum16 = (a: int, b: int, c: int, d: int, e: int, f: int, g: int,
                 h: int, i: int, j: int, k: int, l: int, m: int, n: int,
                 o: int, p: int) => captured + a + b + c + d + e + f + g + h +
                 i + j + k + l + m + n + o + p
    print(sum16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) ++ "\n")
}
