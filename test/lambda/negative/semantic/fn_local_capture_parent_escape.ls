fn invalid() {
    var value = 0
    pn escaped() {
        pn update() { value = value + 1 }
        return 0
    }
    return escaped
}
invalid()
