// ER-S4: an unchecked native stack fault crosses `fn` frames and is captured
// by the nearest non-suspending procedural handler boundary.
pn overflow(n) int^ {
    return n + overflow(n + 1)
}

pn main() {
    var value = null
    overflow(0) ^ { value = ^ } ~ { value = ~ }
    print([value, value is error, value.code, value.message])
}
