// S9.1.4v2: projected aliases cannot export live parent storage.
fn positional(seed) {
    var value, unused = [seed, 0]
    pn writer() { value = 999 }
    pn read() { return value }
    let escaped, spare = [read, 0]
    value = seed + 100
    return escaped
}
fn named(seed) {
    var value = seed
    pn writer() { value = 999 }
    pn read() { return value }
    var escaped, spare at {escaped: read, spare: 0}
    value = seed + 100
    return escaped
}
pn pair(var count: int) {
    count = count + 1
    return [1, 2]
}
pn main() {
    let first = positional(10)
    let second = named(20)
    var count: int = 0
    var a, b = pair(count)
    a = a + 1
    return [first(), second(), a, b, count]
}
