// T22-5b MIR shape: the static spine is prepared once, then the guarded
// integer field fast arm writes its packed slot without a terminal setter.
pn main() {
    var source = {child: {leaf: 1}}
    var changed = source
    changed.child.leaf = 2
    print([source.child.leaf, changed.child.leaf])
}
