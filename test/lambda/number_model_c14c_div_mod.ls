// C14c: `div` and `%` keep their operation semantics but use `/`'s number domain.
fn show(value) => [type(value), value]

fn half_count(n: int) int => int((n + 1) div 2)
fn half_float_count(n: int) float {
    let pairs: float = (n + 1) div 2
    pairs
}

let zero = 0
let numerator = reshape([6, 0, -6, 8], [2, 2])
let divisor = reshape([2, 0, 3, 2], [2, 2])
let vector_div = numerator div divisor
let vector_mod = numerator % divisor
[
    show(7 div 2),
    show(-7 div 2),
    show(-7 % 2),
    show(7 % -2),
    show(1 div zero),
    show(0 div zero),
    show(1 % zero),
    show(0 % zero),
    show(5.0 div 2),
    show(5.0 % 2),
    show(7i8 div 2u8),
    show(7i8 % 2u8),
    show(7i64 div 2u64),
    show(7i64 % 2u64),
    show(half_count(3)),
    show(half_float_count(3)),
    vector_div,
    vector_mod,
    shape(vector_div),
    subview(vector_div, 0, 2),
    subview(vector_mod, 0, 2),
    [1, 0, -1] div zero,
    [1, 0, -1] % zero
]
