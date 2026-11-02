fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    let escaped, unused = [update, 0]
    return escaped
}
invalid()
