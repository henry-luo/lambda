// Regression guard: a call routed to the inferred slow body returns a boxed
// Item even when its inferred static type is a native scalar. Three consumers
// must honor that representation — the declaration binding, the router's
// argument admission for a later call, and a plain reassignment. When any of
// them re-derives "int lane" from the AST type instead, the Item's tag bits
// get boxed as a lane, saturate to the infinity sentinel, and every value
// downstream becomes `inf` (havlak's CFG node ids, Result27).
//
// The wrapper pn matters: the same sequence in main() does not route the
// calls, so it never reproduced the bug.

pn straight(start, n) {
    var i = 0
    while (i < n) { i = i + 1 }
    var r = start + n
    return r
}

pn diamond(start) { return start + 3 }

// shape 1+2: declaration bindings of routed-call results, then feeding one
// binding onward as an inferred-lane argument
pn base_loop(from) {
    var header = straight(from, 1)
    var d1 = diamond(header)
    var d11 = straight(d1, 1)
    return d11
}

// shape 3: reassignment of a routed-call result into an existing binding
pn base_loop_rebind(from) {
    var footer = straight(from, 1)
    footer = straight(footer, 1)
    footer = diamond(footer)
    return footer
}

// the original havlak shape with a container threaded through every call
pn connect(c, a, b) { return 0 }
pn straight3(c, start, n) {
    var i = 0
    while (i < n) {
        var s1 = start + i
        var s2 = s1 + 1
        connect(c, s1, s2)
        i = i + 1
    }
    var r = start + n
    return r
}
pn diamond3(c, start) {
    var bb0 = start
    var bb3 = bb0 + 3
    connect(c, bb0, bb3)
    return bb3
}
pn base_loop3(c, from) {
    var header = straight3(c, from, 1)
    var d1 = diamond3(c, header)
    var d11 = straight3(c, d1, 1)
    return d11
}

pn main() {
    print(base_loop(0))
    print("\n")
    print(base_loop_rebind(0))
    print("\n")
    print(base_loop3(0, 0))
    print("\n")
}
