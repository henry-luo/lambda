// S11.4.1v3: an annotated boundary ends statically proven, statically
// rejected, or in a deferred runtime check -- never unchecked. The JIT skipped
// the deferred checks below: it read every literal as `null` against a
// nullable contract (J1), and bound a concrete value to a contract the checker
// had only deferred -- `T[]?`, `T*`, `N?`, and every reassignment (J2). Each
// case sits in its own procedure, so a failed crossing surfaces as that
// procedure's error value (S11.4.10). Every tier must print the same.

type N = {a: int}

fn f_union(a: int[] | null) { a }
fn f_opt(a: int[]?) { a }
pn p_opt(a: int[]?) { return a }

// J1: a literal against a null-accepting contract
pn decl_literal_union() {
    let w: int[] | null = 5
    return w
}
pn arg_literal_union() { return f_union(5) }

// J2: declarations the checker deferred
pn decl_scalar_opt_array() {
    var v = 5
    let w: int[]? = v
    return w
}
pn decl_array_opt_array() {
    let w: int[]? = ["s"]
    return w
}
pn decl_scalar_run() {
    var v = "s"
    let w: int* = v
    return w
}
pn decl_opt_record() {
    let x: N? = {a: "s"}
    return x
}

// J2: arguments
pn arg_scalar_fn() {
    var v = 5
    return f_opt(v)
}
pn arg_scalar_pn() {
    var v = 5
    return p_opt(v)
}

// J2: reassignments, which the checker never decides
pn reassign_scalar() {
    var x: int = 0
    x = "s"
    return x
}
pn reassign_opt_array() {
    var w: int[]? = null
    w = 5
    return w
}
pn reassign_run() {
    var w: int* = null
    w = "s"
    return w
}

// the same contracts admit what they describe
pn admitted() {
    let u: int[] | null = null
    var w: int[]? = [1, 2]
    w = null
    w = [3]
    var r: int* = null
    r = 5
    var x: N? = null
    x = {a: 1}
    var n: int = 0
    n = n + 1
    return [u, f_union([5]), w, f_opt(null), p_opt([4]), r, x, n]
}

// an accumulator on its own float lane: the relation defers `total + ...`
// (the index read may be null) but the lane already is the contract
pn norms(v: float[], count: int) float {
    var total: float = 0.0
    var i: int = 0
    while (i < count) {
        total = total + math.sqrt(v[i])
        i = i + 1
    }
    return total
}

pn show(label: string, r: any) {
    print(label)
    print(": ")
    if (r is error) { print("error") } else { print(r) }
    print("\n")
}

pn main() {
    show("decl literal union", decl_literal_union())
    show("arg literal union", arg_literal_union())
    show("decl scalar opt array", decl_scalar_opt_array())
    show("decl array opt array", decl_array_opt_array())
    show("decl scalar run", decl_scalar_run())
    show("decl opt record", decl_opt_record())
    show("arg scalar fn", arg_scalar_fn())
    show("arg scalar pn", arg_scalar_pn())
    show("reassign scalar", reassign_scalar())
    show("reassign opt array", reassign_opt_array())
    show("reassign run", reassign_run())
    show("admitted", admitted())
    show("norms", norms([1.0, 4.0, 9.0], 3))
}
