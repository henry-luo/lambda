// S7.7.3/D4.3.1: Error-only edges provide no successful carrier evidence.
fn successor(value) => value + 1
fn selected(flag) any {
    let failure = error("returned")
    if (flag) { return failure }
    return 3
}
pn main() {
    var failure = error("retained")
    let combined = bor(1, failure)
    let rejected = successor(failure)
    let returned = selected(true)
    let normal = selected(false)
    let advanced = successor(4);
    [failure is error, combined is error, rejected is error,
     returned is error, normal == 3, advanced == 5]
}
