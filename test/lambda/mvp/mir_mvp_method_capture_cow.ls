// S9.1.4v2/S9.3.1: captures and receiver snapshots survive COW detaches.
fn make(seed: int) {
    var shift = seed
    type Counter {
        value: int,
        fn total() => value + shift
        pn bump() { value = value + shift }
    }
    shift = shift + 100
    return <Counter value:5>
}
pn index(var count: int) { count = count + 1; return 0 }
pn main() {
    var first = make(10)
    let original = first
    let saved = first.total
    first.bump()^
    let second = make(20)
    var nested = {rows:[make(3)]}
    let before = nested
    var count = 0
    nested.rows[index(count)]["bump"]()^
    let churn = [for (i in 1 to 100) {text:string(i)}]
    print([
        first.value == 15,
        first.total() == 25,
        original.value == 5,
        original.total() == 15,
        saved() == 15,
        second.total() == 25,
        saved == original.total,
        saved == second.total,
        before.rows[0].value == 5,
        before.rows[0].total() == 8,
        nested.rows[0].value == 8,
        nested.rows[0].total() == 11,
        count == 1,
        len(churn) == 100
    ])
}
