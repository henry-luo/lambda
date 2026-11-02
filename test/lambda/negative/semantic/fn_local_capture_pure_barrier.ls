fn invalid() {
    var value = 0
    pn update() { value = value + 1 }
    let pure = () => update
    return 0
}
invalid()
