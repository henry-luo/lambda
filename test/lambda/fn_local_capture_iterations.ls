// S9.1.4v2: each execution of a loop-local declaration owns a fresh binding.
fn owner() {
    var first = null
    var second = null
    var index = 0
    while (index < 2) {
        var local = index
        pn read() { return local }
        if (index == 0) { first = read } else { second = read }
        local = local + 10
        index = index + 1
    }
    return first == second
}
[owner(), owner()]
