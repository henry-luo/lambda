// S9.1.2 / S9.1.3: a `var` argument whose binding was captured by `let` is
// detached before the callee writes through it. A nullable record parameter
// (`var n: N?`) takes the nullable-pointer lane, which used to select the raw
// native entry and skip that detach on the JIT tier. Identical on interp, jit
// and auto.

type N = {key: float, left: N?, right: N?}

pn set_left(var node: N?) {
    node.left = null
}

pn set_key(var node: N) {
    node.key = 2.0
}

// the handle is unannotated so T0 executes this file itself; the annotated
// handle (a typed place-copy member store T0 does not lower) is pinned by
// cow_var_nullable_record_typed_handle.ls
pn rotate_typed(var node: N?) N? {
    var left = node.left
    var branch = left.right
    node.left = branch
    left.right = node
    return left
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
    var b: N? = {key: 1.0, left: {key: 0.5, left: null, right: null}, right: null}
    let before = b
    set_left(b)
    print([before.left == null, b.left == null])
    print("\n")

    // the non-optional contract (boxed entry) keeps the same rule
    var c: N = {key: 1.0, left: null, right: null}
    let c_before = c
    set_key(c)
    print([c_before.key, c.key])
    print("\n")

    // a rotation writes the root and a borrowed child
    var r: N? = make()
    let r_before = r
    let rotated = rotate_typed(r)
    print([keys(r_before), keys(rotated), keys(r)])
    print("\n")

    // repeated calls on an unshared root stay write-through
    var u: N? = make()
    var i = 0
    while (i < 3) {
        set_left(u)
        i = i + 1
    }
    print(keys(u))
    print("\n")
}
