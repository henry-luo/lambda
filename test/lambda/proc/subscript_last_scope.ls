// S7.2.2: `last` is the length minus one of the innermost enclosing
// subscript's container, in a write as in a read (LR07-36). A write had no
// scope of its own and read whatever container the last lowered read left
// behind, so `a[last] = v` failed to compile unless a read of `a` came first.
// The container is evaluated once: `src()[last]` calls `src` once, as the
// interpreter always did. Golden written from the ruling, not from the runtime.

pn src() {
    print("eval ")
    return [10, 20, 30]
}

pn main() {
    var a = [10, 20, 30]
    a[last] = 99
    var b = [1, 2, 3, 4]
    let t = b[0]
    b[last - 1] = 7
    var m = [[1, 2], [3, 4]]
    m[last, 0] = 0
    print([a, t, b, m])
    print("|")
    print(src()[last])
    print("|")
    print(src()[1 to last])
}
