// S7.7.4v2/SI14v3: borrowed whole bindings publish outcomes without lane loss.
pn replace_array_later(var values: int[]) {
    values = fill(3, 9)^
    values[1] = 7
}
pn main() {
    var values: int[] = [1]
    let snapshot = values
    replace_array_later(values)^
    var value: int = 1
    let before = () int => value
    replace_later(value, "bad")
    let after = () any^ => value
    let observed = after()
    var unchanged: int = 7
    let early = propagate_before_store(unchanged);
    [before(), observed is error, value is error, unchanged, early.message, values, snapshot]
}
pn replace_later(var value: int, next) { value = next }
fn failed() int^ => error("before store")
pn propagate_before_store(var value: int) int^ {
    value = failed()^
    return 0
}
