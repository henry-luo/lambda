// S11.1.4/D3.2.5: `<:` compares the admitted value sets of two type values.
type Narrow = {a: int, b: int}
type Wide = {a: int}
type Ints = int[]
type Numbers = number[]
type Shape { color: string }
type Circle : Shape { radius: int }

fn reflexive(T: type) bool => T <: T;

[
    int <: number,
    number <: int,
    int <: int | string,
    Narrow <: Wide,
    Wide <: Narrow,
    Ints <: Numbers,
    Numbers <: Ints,
    Circle <: Shape,
    Shape <: Circle,
    Circle <: Circle,
    reflexive(int)
]
