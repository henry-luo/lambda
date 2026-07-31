// ER-S4: an unchecked native stack fault crosses `fn` frames and is captured
// by the nearest non-suspending procedural `^err` boundary.
fn overflow(n) => n + overflow(n + 1)

pn main() {
    let value^err = overflow(0)
    print([value, ^err, err.code, err.message])
}
