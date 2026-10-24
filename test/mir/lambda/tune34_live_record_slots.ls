// D3.4.6: live descriptors decode nullable slots after widening; snapshots keep their values.
pn make_node(value) { return {tag: value, left: null, right: null} }
pn read_left(node) { return node.left }
pn main() {
    var node = make_node(3)
    let original = node
    print(read_left(node)); print("\n")
    node.left = make_node(5)
    print([read_left(node).tag, read_left(original)]); print("\n")
    node.left = 7
    print(read_left(node)); print("\n")
    node.left = "wide"
    print(read_left(node)); print("\n")
    node.left = null
    print(read_left(node)); print("\n")
    node.left = false
    print(read_left(node)); print("\n")
    node.left = error("retained")
    print(type(read_left(node)) == error); print("\n")
}
