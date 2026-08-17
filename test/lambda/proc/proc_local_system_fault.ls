// ER-S4: an unchecked native stack fault crosses `fn` frames and is captured
// by the nearest non-suspending procedural handler boundary.
fn overflow(n) => n + overflow(n + 1)

pn main() {
    let value = overflow(0) ^ { ^ }
    print([value, value is error, value.code, value.message])
}
