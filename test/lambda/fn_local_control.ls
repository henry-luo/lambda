// S12.1.2v3: local control preserves the fn call boundary.
fn accumulate(n: int) int {
    var i = 0, total = 0
    while (i < n) {
        i = i + 1
        if (i == 3) { continue }
        if (i == 8) { break }
        total = total + i
    }
    return total
}

fn nested() int {
    var i = 0, total = 0
    while (i < 3) {
        i = i + 1
        var j = 0
        while (j < 4) {
            j = j + 1
            if (j == 2) { continue }
            if (j == 4) { break }
            total = total + i * j
        }
    }
    return total
}

fn early(n) {
    while (true) { return n }
}

fn bare() { return }

[accumulate(10), accumulate(0), nested(), early(42), bare()]
