fn invalid() {
    var value = 0
    pn update() { value = value + 1; return value }
    return update
}
invalid()
