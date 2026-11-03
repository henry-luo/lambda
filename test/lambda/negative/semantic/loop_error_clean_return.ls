// SI14v3/S11.4.2v3: a loop back-edge can carry a deferred validation error.
fn opaque(value) => value
pn wrong() int {
    var value: int = 1
    var i = 0
    while (i < 2) {
        if (i > 0) { return value }
        value = opaque("bad")
        i = i + 1
    }
    0
}
wrong()
