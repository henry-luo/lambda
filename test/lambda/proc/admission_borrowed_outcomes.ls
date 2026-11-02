// S7.7.4v2/SI14v3: borrowed whole bindings publish outcomes without lane loss.
pn main() {
    var value: int = 1
    let before = () int => value
    replace_later(value, "bad")
    let after = () any^ => value
    let observed = after()
    var unchanged: int = 7
    let early = propagate_before_store(unchanged);
    [before(), observed is error, value is error, unchanged, early.message]
}
pn replace_later(var value: int, next) { value = next }
fn failed() int^ => error("before store")
pn propagate_before_store(var value: int) int^ {
    value = failed()^
    return 0
}
