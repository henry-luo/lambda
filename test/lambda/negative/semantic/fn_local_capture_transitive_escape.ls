fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    pn escaped() { return update }
    return escaped
}
invalid()
