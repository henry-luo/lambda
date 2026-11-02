// S9.1.4v2/D6.2.3v3: aliases, aggregates and known non-retaining calls stay local.
fn discard(value) => 0
fn relay(value) => value
fn forward(value) => relay(value)
fn relay_named(left, right) => right
fn relay_rest(head, ...) => varg()
fn owner(seed) {
    var value = seed
    pn increment() { value = value + 1 }
    pn nested() {
        pn inner() { value = value + 10 }
        return 0
    }
    discard(nested)
    let alias = increment
    let bundle = {left: [alias], right: pn() => { value = 99 }}
    discard(bundle)
    discard(forward(alias))
    discard(relay_rest(1, alias))
    discard(relay_named(right: alias, left: 1))
    let saved = () => value
    value = value + 2
    // an explicit return discards preceding collected closure values.
    relay(increment)
    if (seed > 0) { return [value, saved()] } else { return [0, saved()] }
}
[owner(10), owner(20)]
