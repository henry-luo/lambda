fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    var escaped, unused at {escaped: update, unused: 0}
    return escaped
}
invalid()
