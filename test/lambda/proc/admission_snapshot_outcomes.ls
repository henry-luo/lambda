// S7.6.3v3/S9.1.2: propagation and error unions preserve container observers.
type Child = {value: int}
type Box = {child: Child, items: Child[]}
fn opaque(value) => value
pn borrowed_snapshot(var box: Box, value) any^ {
    box = value
    let saved = box^
    var current: Box = box^
    current.child.value = 31
    box = current
    return saved
}
pn main() {
    let checked_borrowed: Box = {child: {value: 2}, items: []}
    var borrowed: Box = checked_borrowed^
    let captured = borrowed_snapshot(borrowed, {child: {value: 3}, items: []})
    let admitted: Box = opaque({child: {value: 1}, items: []})
    var plain: Box = admitted^
    let first = plain
    plain.child.value = 9
    let union_snapshot: Box^ = plain
    plain.child.value = 13
    var reused: Box = union_snapshot^
    let reused_before = reused
    reused.child.value = 21
    let child_snapshot: Child^ = plain.child
    plain.child.value = 25
    let failed: Box^ = error("kept")
    let error_copy: Box^ = failed
    let schema = Box | error;
    [first.child.value, union_snapshot^.child.value, reused_before.child.value,
     reused.child.value, child_snapshot^.value, plain.child.value,
     error_copy is error, schema is type, captured^.child.value, borrowed^.child.value]
}
