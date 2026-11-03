fn invalid(retain) {
    var value = 0
    pn update() { value = value + 1 }
    let retained = retain(update)
    return value
}
fn discard(arg) => 0
invalid(discard)
