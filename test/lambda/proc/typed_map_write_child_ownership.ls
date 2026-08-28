// A typed map member write is transactional: it clones the root one level,
// stores on the private candidate, validates, then publishes. The clone marks
// the shared children COW-shared because two maps briefly reference them.
//
// When the pre-image was UNIQUE it dies at that same publish, so those marks
// record sharing that never existed. Under CW3's monotonic 1-bit flag they are
// never cleared, and the next element write through a child took the
// copy-on-write path and published to the LOCAL instead of the map -- while MIR
// Direct wrote in place. Two field writes were enough to trigger it, which is
// why typed jetstream/hashmap2 lost every store on the interpreter tier and
// every lookup returned the EMPTY sentinel.
//
// `var xs = h.xs; xs[i] = v` aliases on both tiers (the C4.1 field-read
// behaviour); these cases pin that it keeps doing so after one, two and three
// field writes, and that a live observer of the ROOT still gets COW's
// guarantee (S9.1.2) because a shared pre-image keeps its marks.

type H = {a: array, b: array, c: array, n: int}

pn mk() H {
    var h: H = {a: fill(4, 0), b: fill(4, 0), c: fill(4, 0), n: 0}
    return h
}

pn write_a(h: H, v: int) {
    // Read-modify-write-back (C4.2e). Binding `h.a` binds a COPY under S9.1.2,
    // so the mutated child is stored back explicitly. What this fixture guards
    // is unchanged: the element write must survive the transactional field
    // writes in main() and land in `h`, and a live observer of the root must
    // still see its own pre-image.
    var la = h.a
    la[0] = v
    h.a = la
    h.n = h.n + 1
}

pn main() {
    // no field write before the aliased element write
    var none: H = mk()
    write_a(none, 1)
    print(none.a[0]) print(" ") print(none.n)
    print("\n")

    // one, two and three field replacements: each clones the root again
    var one: H = mk()
    one.a = fill(4, 0)
    write_a(one, 2)
    print(one.a[0])
    print("\n")

    var two: H = mk()
    two.a = fill(4, 0)
    two.b = fill(4, 0)
    write_a(two, 3)
    print(two.a[0])
    print("\n")

    var three: H = mk()
    three.a = fill(4, 0)
    three.b = fill(4, 0)
    three.c = fill(4, 0)
    write_a(three, 4)
    print(three.a[0])
    print("\n")

    // repeated writes accumulate through the same alias
    var many: H = mk()
    many.a = fill(4, 0)
    many.b = fill(4, 0)
    write_a(many, 5)
    write_a(many, 6)
    print(many.a[0]) print(" ") print(many.n)
    print("\n")

    // a live observer of the ROOT keeps COW's guarantee: the pre-image is
    // shared, so its children keep their marks and the snapshot is unaffected
    var shared: H = mk()
    let snap = shared
    shared.a = fill(4, 0)
    shared.b = fill(4, 0)
    write_a(shared, 9)
    print(snap.a[0]) print(" ") print(shared.a[0])
    print("\n")
}
