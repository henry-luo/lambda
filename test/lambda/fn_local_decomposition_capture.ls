// S9.1.4v2/D6.2.3v3: decomposed targets use the same shared-home protocol.
fn discard(value) => 0
fn local_writer(seed) {
    var a, b = [seed, seed + 1]
    pn update() { a = a + 1; b = b + 1 }
    let alias, unused = [update, 0]
    discard(alias)
    a = a + 2
    return [a, b]
}
fn live() {
    var value, spare = [0, 20]
    var first = null
    var second = null
    var i = 0
    while (i < 2) {
        pn read() { return [value, spare] }
        if (i == 0) { first = read } else { second = read }
        value = value + 1
        spare = spare + 1
        i = i + 1
    }
    return first == second
}
fn fresh() {
    var first = null
    var second = null
    var i = 0
    while (i < 2) {
        var local, spare = [i, i + 10]
        pn read() { return [local, spare] }
        if (i == 0) { first = read } else { second = read }
        local = local + 10
        i = i + 1
    }
    return first == second
}
fn scalars() {
    var small, big = [1e-310, 9223372036854775806i64]
    pn update() { small = 3e-310; big = 1i64 }
    discard(update)
    let before = () => [small, big]
    small = 2e-310
    big = 9223372036854775805i64
    return [before(), small, big]
}
[local_writer(10), local_writer(20), live(), fresh(), scalars()]
