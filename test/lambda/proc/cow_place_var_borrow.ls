// S9.1.2 + S9.1.3: a local copied from a mutable place stays a snapshot even
// when a `var` callee mutates it; only an explicit place borrow writes through.
pn set_value(var node) { node.value = 7 }

pn main() {
    var root = {child: {value: 1}}
    var child = root.child
    set_value(child)
    print(root.child.value); print(" "); print(child.value); print("\n")
}
