// S12.2.1v3/S9.1.3/D4.3.1: an implicit entry filter adds no field contract.
pn store_value(var row, value: any) { row.slot = value }
pn store_explicit(var row: any, value: any) { row.slot = value }
pn bump(var row) { row.count = row.count + 1 }
type Link = {key: float, left: Link?, right: Link?}
fn typed_seed() Link {
    var row: Link = {key: 1.5, left: null, right: null}
    return row
}
pn named_copy() any {
    var row = typed_seed()
    let saved = row
    row.left = typed_seed()
    return [saved.left, row.left.key]
}
pn main() {
    var row = {slot: 1, count: 0}
    let saved = row
    bump(row)^
    bump(row)^
    store_value(row, "changed")^
    bump(row)^
    print([saved.slot, saved.count, row.slot, row.count]); print("\n")
    store_value(row, [3, 4])^
    print(row.slot); print("\n")
    store_value(row, error("kept"))^
    print(row.slot is error); print("\n")
    store_explicit(row, null)^
    print([row.slot, row.count]); print("\n")
    var different = {prefix: 99, slot: false}
    store_value(different, {nested: 7})^
    print([different.prefix, different.slot.nested]); print("\n")
    print(named_copy()); print("\n")
}
