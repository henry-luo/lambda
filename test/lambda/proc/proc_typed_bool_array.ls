// Tune16 C0.A/D-f: bool[] uses the packed ELEM_BOOL byte lane through
// declaration, indexed load, indexed store, and the checked type boundary.

pn main() {
    var flags: bool[] = [true, false, true]
    flags[1] = true
    print(flags[0])
    print(" ")
    print(flags[1])
    print(" ")
    print(flags[2])
}
