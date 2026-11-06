// scalar record witnesses must preserve snapshots and precise homes (S9.1.3v2, D6.3).
// the existing MVP snapshots siblings(box, box); other tiers currently observe the mutation.
type Counter = {count: int}
pn bump(var box: Counter) any { box.count = box.count + 1; box.count = box.count + 1 }
pn recur(var box: Counter, depth: int) any {
    if (depth == 0) { return null }
    let garbage = fill(8, depth)
    recur(box, depth - 1)^
    bump(box)^
}
pn replace(var box: Counter) any { box = {count: 100}; bump(box)^ }
pn escaped(var box: Counter) Counter^ { let saved = box; bump(box)^; return saved }
pn siblings(var box: Counter, other: Counter) int^ { bump(box)^; return other.count }
pn main() {
    var box: Counter = {count: 1}
    let first = box
    bump(box)^
    print(first.count); print(" "); print(box.count); print("\n")
    recur(box, 3)^; print(box.count); print("\n")
    replace(box)^; print(box.count); print("\n")
    let saved = escaped(box)
    print(saved.count); print(" "); print(box.count); print("\n")
    print(siblings(box, box)); print(" "); print(box.count); print("\n")
}
