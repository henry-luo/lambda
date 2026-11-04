// S9.1.2, D6.3.1: inlined var owners retain snapshots and rooted write-back on every exit.
pn store_at(var a: int[], key: int, value: int) { a[key] = value }
pn replace_owner(var a: int[], value: int) { a = fill(3, value)^; store_at(a, 1, value + 1) }
pn nested(var a: int[], var b: int[]) {
    replace_owner(a, 10)^
    replace_owner(b, 20)^
    store_at(a, 2, 30)
}
pn fail_after_write(var a: int[]) int^ {
    store_at(a, 0, 77)
    let bad: int = a[8]
    bad^
    return 99
}
pn observed(var a: int[], snapshot: int[]) int { store_at(a, 0, 88); return snapshot[0] }
pn check() {
    var a: int[] = [1, 2]
    var b: int[] = [3, 4]
    let saved: int[] = a
    nested(a, b)^
    print(a); print(" "); print(b); print(" "); print(saved); print("\n")
    let bad = fail_after_write(a)
    if (not (bad is error)) { print("UNEXPECTED BORROW\n") }
    print(a[0]); print(" "); print(saved[0]); print("\n")
    print(observed(a, a)); print(" "); print(a[0]); print("\n")
    let alias: int[] = a
    store_at(a, 0, 90)
    print(observed(a, alias)); print(" "); print(alias[0]); print(" "); print(a[0]); print("\n")
}
pn main() { check() }
