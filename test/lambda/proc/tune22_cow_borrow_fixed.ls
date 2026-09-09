// S9.2.2: a short static PLACE borrow detaches each link before the callee
// writes, leaving an alias of the root unchanged.
pn tag(var row) { row.value = 17 }

pn main() {
    var root = {rows: [{value: 1}]}
    var snapshot = root
    tag(root.rows[0])
    print([root.rows[0].value, snapshot.rows[0].value])
}
