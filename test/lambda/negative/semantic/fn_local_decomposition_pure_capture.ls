fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    let alias, unused = [update, 0]
    let pure = () => alias
    return 0
}
invalid()
