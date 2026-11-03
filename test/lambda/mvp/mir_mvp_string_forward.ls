// S9.1.2: fusing a single-use string must preserve snapshots and argument order.
pn wrap(a: string, b: string) string { return "<" ++ a ++ ":" ++ b ++ ">" }
pn mark(s: string) string { print(s); return s }
pn integer_text(n: int) string { return string(n) }
pn mixed(a: int, b: int) string { return wrap(integer_text(a), string(b)) }
pn mark_number(n: int) int { print(n); print(" "); return n }
pn effect_format(n: int) string { print("format "); return string(n) }
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
    // S4.1.2: the canonical integer spelling includes the finite endpoints and poison.
    print(mixed(0, -1)); print("\n")
    print(mixed(9007199254740991, -9007199254740991)); print("\n")
    print(mixed(int(inf), int(nan))); print("\n")
    print(mixed(int(-inf), 0)); print("\n")
    print(mixed(mark_number(3), mark_number(4))); print("\n")
    print(wrap(integer_text(5), effect_format(6))); print("\n")
    print("U:" ++ wrap(string(9), "λ")); print("\n")
    var digits: string = "p"
    let snapshot = digits
    for i in 0 to 2 { digits = digits ++ integer_text(i) }
    digits = digits ++ integer_text(7) ++ digits
    print(snapshot); print(" "); print(digits); print("\n")
    print(wrap("", "λ🙂")); print("\n")
    print("a" ++ "" ++ "λ" ++ "🙂"); print("\n")
    print("L" ++ mark("D") ++ "x" ++ "y"); print("\n")
}
