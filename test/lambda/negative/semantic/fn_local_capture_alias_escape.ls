fn relay(value) => value
fn forward(value) => relay(value)
fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    let alias = [update]
    return forward({callback: alias})
}
invalid()
