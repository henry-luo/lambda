// S9.1.4v2/S12.3.3v2: methods retain each declaration's lexical snapshot.
fn make(extra: int, cold: float, payload) {
    type Base {
        value: int,
        fn total() => value + extra
        fn fractional() => cold
        fn retained() => payload
    }
    type Box : Base {
        fn doubled() => 2 * (value + extra)
    }
    fn construct(value: int) => <Box value:value>
    return {instance:<Box value:5>, construct:construct}
}
let first = make(10, 1.0e-310, {items:[1, 2]})
let saved = first.instance.total
let second = make(20, 2.0e-310, {items:[3, 4]})
let later = first.construct(7)
let churn = [for (i in 1 to 100) {text:string(i)}];
[
    first.instance.total() == 15,
    second.instance.total() == 25,
    saved() == 15,
    first.instance.doubled() == 30,
    second.instance.doubled() == 50,
    later.total() == 17,
    later.doubled() == 34,
    first.instance.fractional() == 1.0e-310,
    second.instance.fractional() == 2.0e-310,
    first.instance.retained() == {items:[1, 2]},
    second.instance.retained() == {items:[3, 4]},
    type(first.instance) == type(second.instance),
    len(churn) == 100
]
