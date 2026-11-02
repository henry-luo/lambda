// S12.2.1v3/S16.8.7: each comma target has ordinary var semantics.
fn positional() {
    var a, b = [1, 2]
    a = a + 1
    while (b < 4) { b = b + 1 }
    return [a, b]
}
fn named() {
    var left, right at {left: 10, right: 20}
    left = left + 1
    right = "done"
    return [left, right]
}
fn widening() {
    var a, b = [{n: 1}, {n: 2}]
    a = 3
    b = "done"
    return [a, b]
}
fn missing() {
    var a, b = [1]
    b = 2
    return [a, b]
}
fn copies(source) {
    var first, second = source
    let saved = first
    first.n = 30
    second.n = 40
    return [saved.n, first.n, second.n, source[0].n, source[1].n]
}
fn source_copies() {
    var source = [{n: 10}, {n: 20}]
    let first, second = source
    source[0].n = 30
    source[1].n = 40
    return [first.n, second.n, source[0].n, source[1].n]
}
fn named_copies() {
    var source = {left: {n: 10}, right: {n: 20}}
    let left, right at source
    source.left.n = 30
    source.right.n = 40
    return [left.n, right.n, source.left.n, source.right.n]
}
[positional(), named(), widening(), missing(), copies([{n: 10}, {n: 20}]), source_copies(), named_copies()]
