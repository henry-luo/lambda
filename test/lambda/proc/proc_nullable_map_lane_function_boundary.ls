// A nullable map parameter/return uses its raw pointer lane; null remains zero
// through the checked public adapter and direct body.
fn pass_row(value: map?) map? => value

pn main() {
    let missing: map? = pass_row(null)
    let present: map? = pass_row({count: 7})
    print([missing, present.count])
}
