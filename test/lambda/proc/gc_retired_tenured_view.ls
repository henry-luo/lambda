// A full data-zone compaction used to release the old tenured zone before
// sweep, while dead ArrayNum views still kept their shape side tables there;
// the sweep finalizer then read freed memory (EXC_BAD_ACCESS in
// heap_gc_destroy_external_payload). Walking a shared-subtree int tree creates
// enough short-lived row views to force that compaction.
fn tree(d) {
    if (d == 0) 0
    else {
        let child = tree(d - 1);
        [child, child]
    }
}

pn walk(v, var out) {
    if (v is array) {
        for (x in v) { walk(x, out) }
    } else {
        out.push(v)
    }
}

pn main() {
    let t = tree(14)
    var leaves = []
    walk(t, leaves)
    print("leaves=" ++ string(len(leaves)) ++ " sum=" ++ string(sum(leaves)) ++ "\n")
}
