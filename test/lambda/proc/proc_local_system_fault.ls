// ER-S4: an unchecked native stack fault crosses `fn` frames and is captured
// by the nearest non-suspending procedural handler boundary.
pn overflow(n) int^ {
    return n + overflow(n + 1)
}

pn main() {
    var value = null
    { let __error_outcome_1 = overflow(0) ^ { ^ }; if (__error_outcome_1 is error) { value = __error_outcome_1 } else match __error_outcome_1 { case any { value = ~ } } }
    print([value, value is error, value.code, value.message])
}
