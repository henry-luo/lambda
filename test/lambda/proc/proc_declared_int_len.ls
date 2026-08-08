// Tune16 C0.B/D-d: len-derived arithmetic remains valid in an annotated int
// binding instead of being narrowed or written in the wrong scalar lane.

pn format9(x: float) string {
    var frac_int = int(x)
    var frac_str = string(frac_int)
    var pad: int = 9 - len(frac_str)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    return prefix ++ frac_str
}

pn main() {
    print(format9(1234.0))
    print("\n")
}
