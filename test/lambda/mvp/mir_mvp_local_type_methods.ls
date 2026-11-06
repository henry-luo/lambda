// S12.3.3v2: a local type retains methods at its own callable depth.
fn make(seed: int) {
    type Box {
        value: int,
        fn total(amount: int = 3) => value + amount
        fn self() => ~
    }
    return <Box value:seed>
}
let first = make(5)
let saved = first.total
let second = make(9)
let churn = [for (i in 1 to 60) {text:string(i)}];
[
    first.total() == 8,
    second.total(4) == 13,
    saved() == 8,
    first.self() == first,
    type(first) == type(second),
    len(churn) == 60
]
