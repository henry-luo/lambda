// A self-referential record field is NECESSARILY optional -- the recursion has
// to terminate on null -- and `N?` is a TypeUnary whose OWN type_id is
// LMD_TYPE_TYPE, not LMD_TYPE_MAP. Every map-contract test that asked the
// wrapper for its type_id therefore dropped the map arm, so the crossings that
// REIFY a value into the contract's packed layout were all skipped:
//   * the MIR return firewall, both call-argument sites and the assignment site
//     elided the boundary (transpile-mir.cpp);
//   * runtime_type_admit_value fell through to its lambda_type_matches
//     shortcut and admitted the map structurally, unchanged (lambda-eval.cpp).
//
// Meanwhile the direct field read DOES index by the contract's byte offsets, so
// it read an unreified literal's 9-byte `TypedItem` slot as a bare 8-byte
// Container* and returned the tag byte plus seven pointer bytes as an Item.
// That Item then crashed the next `lambda_type_check` (SIGSEGV on release,
// ASan BUS on debug) -- the traversals below all faulted before printing.
//
// Guards the whole shape: a one-link chain built from literals, a two-link
// (splay-style) tree, a `var`-annotated root, and reassignment through the
// nullable field.
type Node = {val: int, next: Node?}
type Tree = {key: int, left: Tree?, right: Tree?}
type Root = {head: Node?}

pn make_chain(n: int) Node? {
    if (n == 0) { return null }
    // unannotated let: the literal gets NO contract hint, so its own inferred
    // shape is what reaches the declared `Node?` return -- the exact case the
    // return firewall has to reify.
    let e = {val: n, next: make_chain(n - 1)}
    return e
}

pn sum_chain(node: Node?) int {
    if (node == null) { return 0 }
    return node.val + sum_chain(node.next)
}

pn chain_len(node: Node?) int {
    var walk = node
    var n: int = 0
    while (walk != null) {
        n = n + 1
        walk = walk.next
    }
    return n
}

pn make_tree(depth: int, key: int) Tree? {
    if (depth == 0) { return null }
    let t = {key: key, left: make_tree(depth - 1, key * 2),
             right: make_tree(depth - 1, key * 2 + 1)}
    return t
}

pn tree_sum(t: Tree?) int {
    if (t == null) { return 0 }
    return t.key + tree_sum(t.left) + tree_sum(t.right)
}

pn main() {
    let chain = make_chain(100)
    print(sum_chain(chain) ++ " " ++ chain_len(chain) ++ "\n")
    print(chain.val ++ " " ++ chain.next.val ++ " " ++ chain.next.next.val ++ "\n")

    // depth 3 over keys 1,2,3,4..7 -> 1+2+3+4+5+6+7 = 28
    let tree = make_tree(3, 1)
    print(tree_sum(tree) ++ " " ++ tree.left.key ++ " " ++ tree.right.key ++ "\n")

    // an annotated root takes the declaration boundary instead of the return
    // firewall; its nullable field must survive both the store and the read
    var r: Root = {head: make_chain(3)}
    print(sum_chain(r.head) ++ " " ++ r.head.val ++ "\n")
    r.head = make_chain(5)
    print(sum_chain(r.head) ++ " " ++ chain_len(r.head) ++ "\n")
    r.head = null
    print(sum_chain(r.head) ++ " " ++ chain_len(r.head) ++ "\n")
}
