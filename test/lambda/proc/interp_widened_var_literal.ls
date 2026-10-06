// An unannotated `var` initialized with an int and reassigned to arrays may
// change runtime type (S12.2.1). Reads typed before the widening assignment
// keep the int lane in the AST, so T0 built `[w, w]` as a compact int array
// and stored the array-valued `w` as 0; both tiers now build a generic array.
pn main() {
    var w = 0
    var d = 0
    while (d < 3) {
        w = [w, w]
        d = d + 1
    }
    print("depth3=" ++ string(w[0][0][0] == 0) ++ " nested=" ++ string(w[0][0] is array) ++ "\n")
}
