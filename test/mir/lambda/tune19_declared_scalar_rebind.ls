// T19-A: REBINDING a declared scalar must use the same native lane boundary as
// its declaration. `k = perm[i]` on a declared `int` only has to reject the
// read's null arm (the read is `int?` under D2.5.3): one inline sentinel test
// against INT_LANE_NULL that branches past the cold rejection. The declaration
// site already had that fast boundary; the assignment site instead BOXED the
// lane and called lambda_type_check on the straight-line path once per
// iteration, which is how an annotation ended up slower than no annotation.
//
// The assertion is ADJACENCY, not a call count: both lowerings call
// lambda_type_check exactly once. The difference is that the fast one reaches
// it only from the null arm, so the sentinel test is immediately followed by a
// branch. `checked_rebind` is the unproven twin -- a value with no carrier
// proof still crosses the ordinary boxed boundary.

pn last_element() int {
    var perm: int[] = fill(4, 0)
    perm[0] = 2
    perm[1] = 9
    var k: int = 0
    var i: int = 0
    while (i < 4) {
        k = perm[i]
        i = i + 1
    }
    return k
}

pn checked_rebind(source) int {
    var checked: int = 7
    checked = int(source) or 5
    return checked
}

pn main() {
    print("k=" ++ last_element() ++ "\n")
    print("checked=" ++ checked_rebind("not an int") ++ "\n")
}
