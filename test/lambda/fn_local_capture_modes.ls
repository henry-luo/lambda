fn shared() {
    var value = 0
    var a = null
    var b = null
    var i = 0
    while (i < 2) {
        pn read() { return value }
        if (i == 0) { a = read } else { b = read }
        value = value + 1
        i = i + 1
    }
    return a == b
}
fn pure() {
    var value = 0
    var a = null
    var b = null
    var i = 0
    while (i < 2) {
        fn read() => value
        if (i == 0) { a = read } else { b = read }
        value = value + 1
        i = i + 1
    }
    return a == b
}
fn escaping() {
    var value = 0
    var a = null
    var b = null
    var i = 0
    while (i < 2) {
        pn read() { return value }
        if (i == 0) { a = read } else { b = read }
        value = value + 1
        i = i + 1
    }
    return [a, b]
}
let pair = escaping();
[shared(), pure(), pair[0] == pair[1]]
