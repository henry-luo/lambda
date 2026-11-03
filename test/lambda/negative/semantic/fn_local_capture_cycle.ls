fn invalid() {
    var value = null
    pn update() { value = 1 }
    value = [update]
    return 0
}
invalid()
