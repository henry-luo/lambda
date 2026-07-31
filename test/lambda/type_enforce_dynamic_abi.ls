fn sum4(a: int, b: int, c: int, d: int) int {
    a + b + c + d
}

fn sum8(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) int {
    a + b + c + d + e + f + g + h
}

let dynamic_sum4 = sum4
let dynamic_sum8 = sum8

[
    dynamic_sum4(1, 2, 3, 4),
    dynamic_sum8(1, 2, 3, 4, 5, 6, 7, 8)
]
