// D4.4.5 move-out bind: `var h = root.path` whose place is overwritten
// (`root.path = e`) before any write through h or escape of h may bind as a
// borrow. Every case must print what snapshot semantics (S9.1.2) print, on
// every tier.

type N = {key: float, left: N?, right: N?}

pn rotate_typed(var node: N?) N? {
    var left: N? = node.left
    var branch = left.right
    node.left = branch
    left.right = node
    return left
}

pn rotate_plain(var node) {
    var left = node.left
    var branch = left.right
    node.left = branch
    left.right = node
    return left
}

// h is written before the overwrite: the bind must stay a snapshot
pn write_first(var node) {
    var left = node.left
    left.key = 99.0
    node.left = null
    return left
}

// h escapes into another binding before the overwrite
pn escape_first(var node) {
    var left = node.left
    var kept = left
    node.left = null
    left.key = 7.0
    return [left.key, kept.key]
}

pn keys(n) {
    if (n == null) {
        return []
    }
    return [n.key, keys(n.left), keys(n.right)]
}

pn make() N? {
    var leaf: N? = {key: 0.25, left: null, right: null}
    var mid: N? = {key: 0.5, left: null, right: leaf}
    var top: N? = {key: 1.0, left: mid, right: null}
    return top
}

pn main() {
    var k = 0
    var t: N? = null
    while (k < 40) {
        var a: N? = make()
        t = rotate_typed(a)
        k = k + 1
    }
    print(keys(t))
    print("\n")

    // another holder of the moved node keeps its own value
    var c = {key: 1.0, left: {key: 0.5, left: null, right: null}, right: null}
    let holder = [c.left]
    let r2 = rotate_plain(c)
    print([keys(r2), holder[0].right, holder[0].key])
    print("\n")

    var d = {key: 1.0, left: {key: 0.5, left: null, right: null}, right: null}
    let snap_left = d.left
    let w = write_first(d)
    print([w.key, d.left, snap_left.key])
    print("\n")

    var e = {key: 1.0, left: {key: 0.5, left: null, right: null}, right: null}
    print(escape_first(e))
    print("\n")
}
