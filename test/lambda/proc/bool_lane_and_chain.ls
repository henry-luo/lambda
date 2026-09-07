// T21-1c nullable bool lane through nested and/or/not chains (Result37 triangl2): an out-of-range
// bool[] read is null, `x and y` returns x when x is falsy, `x or y` returns y when x is falsy.
pn run() {
    var b: bool[] = fill(3, true)
    b[1] = false
    var i0 = 0
    var i1 = 1
    var i9 = 9
    // in-range chains
    print(b[i0] and b[i0] and (not b[i1]), " ")
    print(b[i0] and b[i1] and (not b[i1]), " ")
    print(b[i1] or b[i0] or b[i1], " ")
    // out-of-range (null) operands on the nullable lane
    print(b[i9] and b[i0], " ")
    print(b[i0] and b[i9], " ")
    print(b[i9] or b[i0], " ")
    print(b[i0] or b[i9], " ")
    print(b[i9] or b[i9], " ")
    print((b[i9] and b[i0]) or b[i1], " ")
    print(not (b[i9] and b[i0]), " ")
    print((b[i0] and b[i9]) and b[i0], " ")
    if ((b[i0] and b[i9]) or b[i0]) { print("T ") } else { print("F ") }
    if ((b[i0] and b[i9]) and b[i0]) { print("T ") } else { print("F ") }
    if (not (b[i0] and b[i9])) { print("T") } else { print("F") }
    print("\n")
}
pn main() { run() }
