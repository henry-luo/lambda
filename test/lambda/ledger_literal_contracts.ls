// S11.1.1v3/S11.1.6v3/S11.2.1 (LR03-44, LR13-14/15): singleton values, exact empty patterns, present null.
type One = 1
type Half = 1.5
type Yes = true
let kind: type = int;
[
    [1] is One[], [2] is One[], [2] is One[1],
    [1.5] is Half[], [2.5] is Half[], [false] is Yes[],
    [1, 2] is [1, One], [1] is [One],
    [] is [], [1, 2] is [], [[1]] is [[]],
    {a: null} is {a: null}, {a: null} is {a: int | null},
    {a: null} is {a: int?}, {a: null} is {a: int},
    kind == int
]
