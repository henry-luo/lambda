// S9.1.2: fusing a single-use string must preserve snapshots and argument order.
pn wrap(a: string, b: string) string { return "<" ++ a ++ ":" ++ b ++ ">" }
pn mark(s: string) string { print(s); return s }
pn main() {
    var out: string = "start"
    let saved: string = out
    for i in 0 to 2 {
        var piece: string = wrap(string(i), "-")
        out = out ++ piece
    }
    print(saved); print("\n")
    print(out); print("\n")
    var kept: string = wrap("k", "v")
    out = out ++ kept
    print(kept); print("\n")
    var self: string = wrap(out, "x")
    out = out ++ self
    print(out); print("\n")
    var ordered: string = wrap(mark("A"), mark("B"))
    out = out ++ ordered
    print("\n"); print(out); print("\n")
    var unicode: string = "z"
    var addition: string = wrap("λ", "🙂")
    unicode = unicode ++ addition
    print(unicode); print("\n")
}
