// T20-2. The concat operand decode is chosen by the operand's STATIC type: a
// string operand unwraps inline, a non-string operand still needs the fn_string
// conversion. Both shapes appear here so the pair is pinned together -- eliding
// the conversion for a value that genuinely needs it would silently concatenate
// raw bits.
pn main() {
    var s: string = ""
    var n: int = 7
    var i: int = 0
    while (i < 3) {
        s = s ++ "x"      // string operand: inline decode, no fn_string
        s = s ++ n        // int operand: conversion required
        i = i + 1
    }
    print(s)
    print("\n")
}
