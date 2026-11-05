// S16.8.7/S9.1.2: targets keep independent snapshots of one evaluated source.
let alpha, beta = [3, 4]
let width, height at {width: 7, height: 6}
fn globals() => alpha + beta
fn first_value(values) {
    let first, second = values
    return first
}
fn local(seed) {
    var left, right = [seed, seed + 1]
    left = "changed"
    right = right + 2
    return [left, right]
}
fn copies(source) {
    var left, right at source
    let saved = left
    left.n = 30
    right.n = 40
    return [saved.n, left.n, right.n, source.left.n, source.right.n]
}
let changed = local(10)
let copied = copies({left: {n: 1}, right: {n: 2}})
let first, absent = [8];
[
    alpha + beta == 7, width * height == 42,
    changed[0] == "changed", changed[1] == 13,
    copied == [1, 30, 40, 1, 2], absent == null,
    (let alpha, beta = [5, 6], alpha * beta) == 30,
    first == 8, alpha == 3, globals() == 7,
    first_value([1.000000000000001, 2.5]) == 1.000000000000001,
    first_value([{n: 9}, {n: 10}]).n == 9
]
