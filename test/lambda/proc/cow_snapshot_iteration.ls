// CW30/S9.2.3: iteration over a `var` container mutated in the body walks the
// ENTRY-TIME value on both tiers; the mutation reaches the binding. A loop
// whose body does not write its collection emits no snapshot work at all.

pn bump_last(var xs: any[]) { xs[2] = 55 }

pn main() {
    // element write during iteration: loop sees 1,2,3; binding sees 99
    var arr = [1, 2, 3]
    var seen = 0
    for (x in arr) {
        arr[2] = 99
        seen = seen + x
    }
    print(seen); print(" "); print(arr[2]); print("\n")

    // push during iteration: length AND values snapshot; pushes are kept
    var grow = ["a", "b", "c"]
    var count = 0
    for (x in grow) {
        push(grow, "z")
        count = count + 1
    }
    print(count); print(" "); print(len(grow)); print("\n")

    // map field write during iteration: loop sums entry-time values
    var m = {a: 1, b: 2, c: 3}
    var total = 0
    for (v in m) {
        m.c = 77
        total = total + v
    }
    print(total); print(" "); print(m.c); print("\n")

    // mutation through a `var`-param borrow inside the body (NM-O4):
    // the detach-at-borrow and the head snapshot must not fight
    var bor: any[] = ["x", "y", "z"]
    var walked = ""
    for (v in bor) {
        bump_last(bor)
        walked = walked ++ v
    }
    print(walked); print(" "); print(bor[2]); print("\n")

    // inner level of a multi-level loop re-snapshots per outer iteration
    var xs = [1, 2]
    var ys = ["p", "q"]
    var out = ""
    for (a in xs, b in ys) {
        ys[1] = "Z"
        out = out ++ b
    }
    print(out); print(" "); print(ys[1]); print("\n")

    // inner level over a RUNTIME-built array (a const literal is born shared
    // and would mask a missing nested-level mark)
    var rxs = [1, 2]
    var rys = []
    push(rys, "p")
    push(rys, "q")
    var rout = ""
    for (a in rxs, b in rys) {
        rys[1] = "Z"
        rout = rout ++ b
    }
    print(rout); print(" "); print(rys[1]); print("\n")

    // functional default: an untouched collection loops with no COW work
    var quiet = [4, 5, 6]
    var sum = 0
    for (x in quiet) { sum = sum + x }
    print(sum); print("\n")
}
