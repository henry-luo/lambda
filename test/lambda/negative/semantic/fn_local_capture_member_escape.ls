fn invalid() {
    var value = {n: 0}
    pn update() { value.n = 1 }
    return {update: update}
}
invalid()
