fn relay(...) => varg()
fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    return relay(1, update)
}
invalid()
