// S9.1.4v2: direct calls, later aliases and sibling calls use declaration snapshots.
fn direct_scalar() {
    var value = 1
    fn read() => value
    value = 3
    return read()
}
fn direct_array() {
    var xs = [1, 2]
    fn read() => xs[0]
    xs[0] = 3
    return read()
}
fn later_alias() {
    var value = 1
    fn read() => value
    value = 3
    let reader = read
    return reader()
}
fn sibling() {
    var value = 1
    fn read() => value
    fn relay() => read()
    value = 3
    return relay()
}
fn repeated() {
    var value = 1
    fn read() => value
    value = 3
    let first = read()
    value = 5
    return [first, read(), value]
}
fn named_arguments() {
    var value = 1
    fn read(a: int = 10, b: int = 20) => value + a - b
    value = 3
    return read(b: 5, a: 7)
}
[direct_scalar(), direct_array(), later_alias(), sibling(), repeated(), named_arguments()]
