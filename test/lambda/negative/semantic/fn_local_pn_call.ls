fn invalid() {
    var value = 0
    pn update() { return value }
    while (value == 0) { update() }
    return value
}
invalid()
