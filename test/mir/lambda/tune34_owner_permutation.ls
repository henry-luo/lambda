// S9.1.2/D4.4.1: owner cycles preserve retained snapshots and live temporaries.
pn rotate_rows(n) {
    var a = fill(n, 1)
    var b = fill(n, 2)
    var c = fill(n, 3)
    let snapshot = a
    var pass = 0
    while (pass < 3) {
        a[0] = pass + 10
        var saved = a
        a = b
        b = c
        c = saved
        pass = pass + 1
    }
    return [a[0], b[0], c[0], snapshot[0]]
}
pn live_temporary() {
    var a = fill(2, 1)
    var b = fill(2, 2)
    var saved = a
    a = b
    b = saved
    saved[0] = 99
    return [a[0], b[0], saved[0]]
}
pn typed_cycle() {
    var a: int[] = fill(2, 1)
    var b: int[] = fill(2, 2)
    var saved = a
    a = b
    b = saved
    a[0] = 3
    return [a[0], b[0]]
}
pn main() {
    print([rotate_rows(1), rotate_rows(2)]); print("\n")
    print(live_temporary()); print("\n")
    print(typed_cycle()); print("\n")
}
