// S16.9.7/D3.3.1/D6.2.3: each callable owns positional rest slots and scalar bytes.
fn slots(...) => varg()
fn layout(head, count = len(varg())^, ...) any^ => [head, count, varg()]
fn nested(...) {
    let before = varg()
    let empty = layout(head: 1)
    let inner = slots(9, 8)
    let allocate = [for (i in 1 to 40) {index: i, text: string(i)}]
    return [varg() == before, len(varg()) == 3, varg(0) == "outer",
        empty == [1, 0, []], inner == [9, 8], len(allocate) == 40]
}
fn inspect(...) {
    let before = string(varg())
    let allocate = [for (i in 1 to 80) [float(i), string(i)]]
    return [string(varg()) == before, varg(0) == 9007199254740993i64,
        varg(1) == 1.0e-310, len(allocate) == 80]
}
let functions = {slots: slots, inspect: inspect}
let base = 10
let closure = (head, ...) => [base + head, varg()];
[
    slots() == [],
    slots(null, "a", "b", (1, 2), [3, 4]) == [null, "a", "b", [1, 2], [3, 4]],
    functions.slots(null, "a", "b", (1, 2)) == [null, "a", "b", [1, 2]],
    layout(head: 1) == [1, 0, []],
    layout(count: 2, head: 1, 3, 4) == [1, 2, [3, 4]],
    closure(2, "x", null) == [12, ["x", null]],
    call(slots, [null, "a", "b", [1, 2]]) == [null, "a", "b", [1, 2]],
    nested("outer", null, [1, 2]),
    inspect(9007199254740993i64, 1.0e-310),
    functions.inspect(9007199254740993i64, 1.0e-310)
]
