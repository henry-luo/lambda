// Stage-1 COW nested mutation stays register/root-slot owned: no temporary
// Lambda path array is allowed between the detached root and raw link stores.
// Checked by cow_nested_store.mir-check.

pn main() {
    var source = { child: { leaf: 1 } }
    var changed = source
    changed.child.leaf = 2
}
