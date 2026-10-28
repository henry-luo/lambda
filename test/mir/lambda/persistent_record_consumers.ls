// D3.2.4v4/S11.4.10: optional roots and returned values retain admitted layouts.
type Record = {value: int, next: Record?}
type Holder = {child: Record?}

pn set_optional(var record: Record?, value: int) {
    record.value = value
}

pn set_link(var record: Record?, next: Record?) {
    record.next = next
}

pn set_dynamic(var record: Record?, value) {
    record.value = value
}

pn set_nested(var holder: Holder?, value: int) {
    holder.child.value = value
}

pn make_record(present: bool) Record? {
    if (present) { return {next: null, extra: "kept", value: 7} }
    return null
}

pn read_return(present: bool) {
    var record = make_record(present)
    return record.value
}

pn read_rebound() {
    var record = make_record(true)
    record = {other: "unrelated"}
    return record.value
}

pn mutate_return(var record: Record?) {
    var child = make_record(true)
    child.value = "bad"
    record.next = child
}

pn make_chain() Record {
    return {value: 1, next: {value: 2, next: null}}
}

pn mutate_nested_return(var record: Record?) {
    var tree = make_chain()
    tree.next.next = {other: 3}
    record.next = tree.next.next
}

pn erase_child(var child) {
    child.next = {other: 3}
}

pn mutate_borrowed_child(var record: Record?) {
    var tree = make_chain()
    erase_child(tree.next)
    record.next = tree.next.next
}

pn null_field(record: Record?) Record {
    return {value: record.value, next: null}
}

pn admit_record(value) Record? { return value }

pn effect_value() int {
    print("rhs\n")
    return 1
}

pn effectful_store(var record: Record?) {
    record.value = effect_value()
}

pn main() {
    var record: Record? = make_record(true)
    let before = record
    set_optional(record, 9)
    var child: Record? = {value: 11, next: null}
    set_link(record, child)
    print([before.value, record.value, record.extra, record.next.value]); print("\n")
    let rejected = set_dynamic(record, "bad")
    print([rejected is error, record.value]); print("\n")
    let rejected_child = mutate_return(record)
    print([rejected_child is error, record.next.value]); print("\n")
    let rejected_nested = mutate_nested_return(record)
    print([rejected_nested is error, record.next.value]); print("\n")
    let rejected_borrow = mutate_borrowed_child(record)
    print([rejected_borrow is error, record.next.value, null_field(null) is error]); print("\n")
    print([admit_record([1, 2]) is error, admit_record(null)]); print("\n")
    var holder: Holder? = {child: record}
    let holder_before = holder
    set_nested(holder, 13)
    print([holder_before.child.value, holder.child.value, record.value]); print("\n")
    print([read_return(true), read_return(false), read_rebound()]); print("\n")
    var absent: Record? = null
    let null_store = effectful_store(absent)
    print([null_store is error, absent]); print("\n")
}
