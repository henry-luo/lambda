// A plain (non-`var`) parameter of a `pn` is locally mutable under the current
// pn ABI, and its typed-container writes stay VISIBLE TO THE CALLER. MIR Direct
// implemented that (`is_var_param || is_proc_param` selects the in-place checked
// setter); T0 consulted only `is_var_param`, so it validated a DETACHED
// candidate and republished it to the callee's own slot -- the write showed up
// inside the procedure and vanished at the caller.
//
// That is a tier mismatch, not just a slow path: typed `awfy/json2.ls` threads
// its parser state through `p: Parser`, so on the interpreter every
// `p.cur = ...` was lost and the parse returned a number instead of the object.
// The untyped variant passed, which is why it went unnoticed.
//
// These cases pin the rule on BOTH tiers: typed map field, typed array element,
// and the untyped/`var` forms that already agreed.

type P = {cur: string, n: int}
type Box = {xs: int[]}

pn set_typed_field(p: P) {
    p.cur = "X"
    p.n = 7
}

pn set_typed_elem(b: Box) {
    // Read-modify-write-back (C4.2e): binding a field binds a COPY under
    // S9.1.2, so the copy is stored back explicitly. The rule pinned here is
    // unchanged -- the write reaches the caller -- and it now travels the flat
    // typed member assignment, which is the path `is_proc_param` selects the
    // in-place setter for.
    var xs: int[] = (b.xs)
    xs[0] = 99
    b.xs = xs
}

pn set_untyped_field(p) {
    p.cur = "U"
}

pn set_var_field(var p: P) {
    p.cur = "V"
}

pn read_only(p: P) int {
    return p.n
}

pn main() {
    // typed map field through a plain pn parameter
    var a: P = {cur: "~", n: 0}
    set_typed_field(a)
    print(a.cur) print(" ") print(a.n)
    print("\n")

    // typed array element through a plain pn parameter
    var b: Box = {xs: [1, 2, 3]}
    set_typed_elem(b)
    print(b.xs[0]) print(" ") print(b.xs[1])
    print("\n")

    // untyped parameter: already agreed, kept so a fix here cannot regress it
    var c = {cur: "~"}
    set_untyped_field(c)
    print(c.cur)
    print("\n")

    // explicit `var` parameter: the documented sharing construct
    var d: P = {cur: "~", n: 0}
    set_var_field(d)
    print(d.cur)
    print("\n")

    // a parameter that is only read must not be disturbed by the in-place rule
    var e: P = {cur: "z", n: 42}
    print(read_only(e)) print(" ") print(e.cur) print(" ") print(e.n)
    print("\n")

    // repeated calls accumulate on the same caller value
    var f: P = {cur: "~", n: 0}
    set_typed_field(f)
    f.n = f.n + 1
    set_typed_field(f)
    print(f.cur) print(" ") print(f.n)
    print("\n")
}
