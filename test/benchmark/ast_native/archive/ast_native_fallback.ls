// Unsupported local stores/loops and self-tail calls retain their T0 semantics.
pn proc_scalar(n) { return n + 1 }
fn fallible(n) int^ {
    return n / n
}
fn sum_to(n) {
    var total = 0
    var i = 0
    while (i <= n) {
        total = total + i
        i = i + 1
    }
    return total
}
fn tail_count(n, total) {
    if (n == 0) { return total }
    return tail_count(n - 1, total + 1)
}
[sum_to(10), tail_count(1000, 0), fallible("bad") is error, fallible(1)]
