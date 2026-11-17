// S7.1.2v2/S7.10.7: invalid slices join as empty fragments; errors still propagate.
fn bracket(text: string, end: any) string^ => "[" ++ slice(text, 0, end) ++ "]"

pn main() {
    print([bracket("abc", 2), bracket("abc", "bad") == "[]", bracket("xyz", 2),
        bracket("abc", null), bracket("abc", 1.5) == "[]", bracket("abc", -2),
        (bracket("abc", error("offset")) ^ { ^.message }) == "offset"])
}
