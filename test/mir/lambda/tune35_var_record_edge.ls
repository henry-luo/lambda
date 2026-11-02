// S9.1.2-S9.1.3: a direct boxed body must consume and publish the caller home.
type Node = {value: int, next: Node?}

pn advance(var node: Node?, remaining: int) Node? {
    if (node == null or remaining == 0) { return node }
    var child: Node? = node.next
    child = advance(child, remaining - 1)
    node = child
    return node
}

pn change(var node: Node?, value: int) {
    if (node != null) { node.value = value }
}

pn dynamic(var node: Node?, value) { change(node, value) }

pn main() {
    var root: Node? = {value: 1, next: {value: 2, next: null}}
    let snapshot = root
    change(root, 5)
    print([snapshot.value, root.value]); print("\n")
    advance(root, 1)
    print([snapshot.value, root.value]); print("\n")
    advance(root, 1)
    print([root, snapshot.next.value]); print("\n")
    var other: Node? = {value: 4, next: null}
    print(type(dynamic(other, "wrong") ^ { null })); print("\n")
    print(other.value); print("\n")
}
