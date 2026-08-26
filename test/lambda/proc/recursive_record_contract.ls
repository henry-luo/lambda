// A recursive record contract (`left: Node?`) must be usable at native speed.
//
// The load-bearing detail is `payload`, whose value arrives through an UNTYPED
// parameter. Before contract adoption, one unprovable field denied the whole
// literal its declared shape, so every instance allocated its own TypeMap,
// shape identity never held, and each declared crossing re-walked the whole
// reachable structure -- O(n) per admission, O(n^2) overall (Tune19 §11).
// Adoption builds the literal in the contract's shape and ADMITS the unproven
// field at construction instead.
//
// Admitting at construction makes the constructor FALLIBLE -- `make` returns
// `Node^` -- which is the visible semantic consequence of the change and is
// asserted here: a good payload builds, a bad one raises and is containable.
//
// SCOPE: this is a CORRECTNESS test. It passes on a pre-adoption build too (the
// rejection just reports at the declaration rather than at the field), so it is
// not a tripwire for the O(n^2) walk -- that regression is guarded by the splay
// benchmark and the scaling numbers in Tune19 §11.

type Node = {key: float, left: Node?, payload: map?}

fn dynamic(value) any { value }

// `payload` is untyped on purpose -- this is the shape adoption enables.
pn make(k: float, payload) Node^ {
    var n: Node = {key: k, left: null, payload: payload}
    return n
}

pn build(depth: int) Node^ {
    var head: Node = make(0.0, null)^
    var i: int = 1
    while (i < depth) {
        var next: Node = make(float(i), null)^
        next.left = head
        head = next
        i = i + 1
    }
    return head
}

pn depth_of(node: Node?) int {
    var n = node
    var d: int = 0
    while (n != null) {
        d = d + 1
        n = n.left
    }
    return d
}

pn main() {
    let head = build(300)^
    var holder: Node = make(99.0, dynamic({tag: "ok"}))^
    holder.left = head
    // a non-map payload must not reach a `map?` field
    var err = null
    make(1.0, dynamic("not a map")) ^ { err = ^ }
    print(depth_of(holder) ++ " " ++ holder.key ++ " " ++
        holder.payload.tag ++ " " ++ (err is error) ++ "\n")
}
