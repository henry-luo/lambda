// S9.1.3/S1.6: a tail call retains the write-back home of each borrowed place.
pn walk(var node, depth: int) {
    if (depth > 0) { node.k = depth; walk(node.next, depth - 1) }
}
pn walk_named(var node, depth: int) {
    if (depth > 0) { node.k = depth; walk_named(depth: depth - 1, node: node.next) }
}
pn walk_index(var node, depth: int) {
    if (depth > 0) { node.k = depth; walk_index(node.children[0], depth - 1) }
}
pn rotate(var left, var right, depth: int) {
    if (depth > 0) { left.k = left.k + 1; rotate(right, left, depth - 1) }
}
pn same_home(var node, depth: int) {
    if (depth > 0) { node.k = node.k + 1; same_home(node, depth - 1) }
}
pn main() {
    var a = {k:0,next:{k:0,next:{k:0}}}
    walk(a, 2)
    print([a.k, a.next.k, a.next.next.k]); print("\n")
    var b = {k:0,next:{k:0,next:{k:0}}}
    walk_named(b, 2)
    print([b.k, b.next.k, b.next.next.k]); print("\n")
    var c = {k:0,children:[{k:0,children:[{k:0}]}]}
    walk_index(c, 2)
    print([c.k, c.children[0].k, c.children[0].children[0].k]); print("\n")
    var left = {k:0}
    var right = {k:0}
    rotate(left, right, 3)
    print([left.k, right.k]); print("\n")
    var counter = {k:0}
    same_home(counter, 10000)
    print(counter.k); print("\n")
}
