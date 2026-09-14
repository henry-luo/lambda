// Tune27 T27-4 / S9.2.2: a builtin in-place mutator whose first argument is a
// PLACE borrows that place (cow_place_leaf*) and appends raw; a scalar
// insertion needs no capture (T27-7). The aliased slot detaches once.

pn main() {
    var m = {a: [], b: []}
    var x = ["q"]
    m.a = x
    m.b = x
    push(m.a, 1)
    print([m.a, m.b, x])
    print("\n")
    var acc = {items: []}
    var i = 0
    while (i < 3) {
        push(acc.items, i)
        i = i + 1
    }
    print(acc.items)
    print("\n")
}
