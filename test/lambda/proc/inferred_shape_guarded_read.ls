// T20-1a. A map literal already carries a complete per-site TypeMap -- shape
// chain, byte_offsets, byte_size -- and transpile_map writes that pointer into
// the object header as a compile-time constant. What an INFERRED shape lacks is
// the proof a declared contract gets from admission: nothing says the map
// arriving at a member site was built at that site. So the read is emitted
// behind a guard on the header's type word, with the generic accessor as the
// fallback arm.
//
// The guard proves the map's SHAPE, never a field's storage class: fn_map_set
// retags a slot IN PLACE -- same TypeMap, same offset -- for NULL->T and among
// the pointer-like classes. Only a width-changing write rebuilds the shape into
// a NEW TypeMap, which is what the guard actually catches. These cases pin both
// halves: the hit must read the right slot, and every way of reaching the site
// with a map that is NOT this shape must fall back.

pn read_x(m) {
    return m.x
}

pn main() {
    // hit: literal-initialized local read in the same scope
    var a = {x: 1, y: 2}
    print(a.x)
    print(" ")
    print(a.y)
    print("\n")

    // a second construction site with the same structure is a DIFFERENT
    // TypeMap, so this read site must not assume the first one's identity
    var b = {x: 10, y: 20}
    print(read_x(a))
    print(" ")
    print(read_x(b))
    print("\n")

    // a differently-shaped map through the same read site: `x` sits at another
    // offset, so a shape-blind load would return `z`
    var c = {z: 0, x: 99}
    print(read_x(c))
    print("\n")

    // width-changing write: int -> string rebuilds the shape into a new
    // TypeMap, and the guard must miss from here on
    var d = {x: 7, y: 8}
    print(d.x)
    d.x = "str"
    print(" ")
    print(d.x)
    print("\n")

    // in-place retag: a NULL slot upgraded to MAP keeps the same TypeMap, which
    // is why NULL-typed fields stay on the generic accessor
    var n = {val: 1, next: null}
    print(n.next)
    n.next = {val: 2, next: null}
    print(" ")
    print(n.next.val)
    print("\n")

    // container field: the packed slot is a raw Container*, already an Item
    var h = {items: [1, 2, 3]}
    print(len(h.items))
    print("\n")

    // null receiver has no header to test; `null.k` semantics stay generic
    var z = null
    print(z.x)
    print("\n")

    store_cases()
}

// T20-1d: the same shape guard on the STORE side. A store may not retag a slot
// (that bookkeeping belongs to fn_map_set), so it admits only writes whose value
// already carries the slot's lane; everything else stays on the generic setter.
pn store_cases() {
    var p = {x: 1, y: 2}
    p.x = 42
    print(p.x)
    print(" ")
    print(p.y)
    print("\n")

    // a differently-shaped map must not be written through p's offsets
    var q = {y: 7, x: 8}
    q.x = 9
    print(q.x)
    print(" ")
    print(q.y)
    print("\n")

    // a value of another type retags the slot: generic setter, and the read
    // must still see it
    var r = {v: 1}
    r.v = "text"
    print(r.v)
    print("\n")

    dynamic_store_cases()
}

// The store's fast arm proves the VALUE's lane at run time rather than demanding
// a static type, which is what makes it reach untyped code at all. These pass
// dynamically-typed values through one store site: matching lanes take the raw
// write, mismatching ones must fall back to the retagging setter.
pn pass_through(v) {
    return v
}

pn dynamic_store_cases() {
    var m = {n: 0, s: "a", f: 1.5, c: [1]}

    // same lane, dynamic value -> fast arm
    m.n = pass_through(7)
    m.s = pass_through("bee")
    m.f = pass_through(2.5)
    m.c = pass_through([1, 2])
    print(m.n) print(" ") print(m.s) print(" ") print(m.f) print(" ") print(len(m.c))
    print("\n")

    // wrong lane through the same sites -> must retag via the generic setter
    m.n = pass_through("now a string")
    m.s = pass_through(99)
    m.f = pass_through(null)
    print(m.n) print(" ") print(m.s) print(" ") print(m.f)
    print("\n")

    // and the map still reads back consistently afterwards
    var k = 0
    for (key in m) {
        k = k + 1
    }
    print(k)
    print("\n")
}
