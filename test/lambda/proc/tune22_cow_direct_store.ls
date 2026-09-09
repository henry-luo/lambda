// T22-5b: a static nested int store detaches its COW spine before the direct
// packed-slot write, so the snapshot remains unchanged (S9.1.2, S9.3.1).
pn main() {
    var source = {child: {leaf: 1}}
    var changed = source
    changed.child.leaf = 2
    print([source.child.leaf, changed.child.leaf])
}
