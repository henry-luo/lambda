// The retag exists so GC keeps tracing a slot the literal opened as `null`.
// `e.next = make_list(...)` and `e.tag = [...]` are exactly those writes: the
// shape records NULL and the store must publish the pointer lane. Under a forced
// collection on every allocation, a missed retag drops the tail.
pn make_list(n: int) {
    if (n == 0) {
        return null
    }
    var e = {val: 0, next: null, tag: null}
    e.val = n
    e.next = make_list(n - 1)
    e.tag = [n, n]
    return e
}

pn total(node) int {
    if (node == null) {
        return 0
    }
    var s: int = node.val
    if (node.tag != null) {
        s = s + len(node.tag)
    }
    return s + total(node.next)
}

pn main() {
    let l = make_list(300)
    print("sum=")
    print(total(l))
    print("\n")
}
