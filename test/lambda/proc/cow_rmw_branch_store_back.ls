// CW36 / D4.4.4v3 (S9.1.2): a read-modify-write handle may borrow its place
// when the store-back sits inside a branch, provided no path observes the
// place between an in-place write and its store-back, and no path leaves the
// handle's scope with a write unstored. A store-back followed by more uses of
// the handle keeps its capture, so later writes copy. Every case below prints
// what the snapshot bind would print.
type N = {key: float, left: N?, right: N?}
type K = {n: int}
type R = {v: int, kid: K}

// the splay rotation shape: sibling ifs, a store-back in each, a root rebind
// after the first one, a nested handle passed to a recursive `var` call
pn rot_r(var node: N?) N? {
    var left: N? = node.left
    node.left = left.right
    left.right = node
    return left
}
pn rot_l(var node: N?) N? {
    var right: N? = node.right
    node.right = right.left
    right.left = node
    return right
}
pn sp(var node: N?, key: float) N? {
    if (node == null) { return null }
    if (key < node.key) {
        if (node.left == null) { return node }
        var left: N? = node.left
        if (key < left.key) {
            var branch: N? = left.left
            branch = sp(branch, key)
            left.left = branch
            node.left = left
            node = rot_r(node)
        }
        if (key > left.key) {
            var branch: N? = left.right
            branch = sp(branch, key)
            left.right = branch
            if (left.right != null) { left = rot_l(left) }
            node.left = left
        }
        if (node.left == null) { return node }
        return rot_r(node)
    }
    return node
}
pn keys(t: N?) string {
    if (t == null) { return "." }
    return "(" ++ keys(t.left) ++ string(t.key) ++ keys(t.right) ++ ")"
}

// both arms taken: a snapshot between the store-backs keeps its value
pn both_arms(var r: R) {
    var h = r.kid
    if (r.v > 0) {
        h.n = h.n + 1
        r.kid = h
    }
    let snap = r.kid
    if (r.v > 0) {
        h.n = h.n + 100
        r.kid = h
    }
    print(snap.n) print(r.kid.n)
}

// a path that writes and never stores back refuses the borrow
pn no_store_back(var r: R, flag: bool) {
    var h = r.kid
    if (flag) { h.n = 99 }
    if (flag) { r.kid = h }
    print(r.kid.n)
}

// an alias of the place taken before the bind keeps the old value
pn alias_before(var r: R) {
    let old = r.kid
    var h = r.kid
    if (true) {
        h.n = 7
        r.kid = h
    }
    print(old.n) print(r.kid.n)
}

// reading the place between the write and the store-back sees the old value
pn read_between(var r: R) {
    var h = r.kid
    if (true) {
        h.n = 5
        print(r.kid.n)
        r.kid = h
    }
    print(r.kid.n)
}

// the caller observes a branch store-back through its `var` argument
pn caller_sees(var r: R, bump: int) {
    var h = r.kid
    if (bump > 0) {
        h.n = h.n + bump
        r.kid = h
    }
}

// an error exit after a write through the handle: no store-back happened
pn error_exit(var r: R, fail: bool) int^ {
    var h = r.kid
    if (fail) {
        h.n = 55
        raise error("stop")
    }
    if (true) {
        h.n = h.n + 1
        r.kid = h
    }
    return r.kid.n
}

// a write hidden in a declaration initializer while an unmutated copy lives
pn mutate(var r: R) int {
    r.kid.n = 7
    return 1
}
pn declaration_write(var r: R) {
    let old = r.kid
    var z = mutate(r)
    print(old.n) print(z) print(r.kid.n)
}

// the handle passed to a procedure defined later, stored back in a branch
pn forward_call(var r: R) {
    let before = r.kid
    var h = r.kid
    if (r.v > 0) {
        later_bump(h)
        r.kid = h
    }
    print(before.n) print(r.kid.n)
}
pn later_bump(var k: K) { k.n = k.n + 1000 }

pn main() {
    var t: N? = {key: 50.0, left: {key: 30.0, left: {key: 10.0, left: null, right: null},
        right: {key: 40.0, left: null, right: null}}, right: null}
    let before = t
    t = sp(t, 10.0)
    print(keys(t))
    print(keys(before))
    t = sp(t, 40.0)
    print(keys(t))
    t = sp(t, 40.0)
    print(keys(t))

    var r1: R = {v: 1, kid: {n: 0}}
    both_arms(r1)
    var r2: R = {v: 1, kid: {n: 1}}
    no_store_back(r2, false)
    print(r2.kid.n)
    var r3: R = {v: 1, kid: {n: 3}}
    alias_before(r3)
    print(r3.kid.n)
    var r4: R = {v: 1, kid: {n: 2}}
    read_between(r4)
    var r5: R = {v: 1, kid: {n: 10}}
    let keep = r5
    caller_sees(r5, 5)
    print(r5.kid.n) print(keep.kid.n)
    var r6: R = {v: 1, kid: {n: 20}}
    error_exit(r6, true) ^ { print("raised") }
    print(r6.kid.n)
    error_exit(r6, false) ^ { print("raised") } ~ { print(~) }
    var r7: R = {v: 1, kid: {n: 3}}
    declaration_write(r7)
    var r8: R = {v: 1, kid: {n: 4}}
    forward_call(r8)
    print(r8.kid.n)
}
