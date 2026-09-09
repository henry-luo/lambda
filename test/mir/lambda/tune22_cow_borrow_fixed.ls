// S9.2.2/D8.4.1v2: static member/int PLACE paths use the bounded descriptor
// ABI and retain the same observable COW isolation as the generic walker.
pn tag(var row) { row.value = 17 }

pn main() {
    var root = {rows: [{value: 1}]}
    var snapshot = root
    tag(root.rows[0])
    print([root.rows[0].value, snapshot.rows[0].value])
}
