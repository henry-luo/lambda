// S11.4.3 (LR12-25): an error operand never enters a system function's
// `any \ error` parameter -- the call's value is that error, and it flows on
// like any value (S7.7.1: expression interiors never skip). Both tiers had
// returned it from the enclosing function instead, and the JIT's `len`/`ord`
// lowering could read the join's bits as the result.
fn chk(v: int) { v * 2 }
fn dyn(v) => v
// a declared binding skips on it (S7.7.2); an unannotated one holds it
fn decl_len(m: string | error) { let n: int = len(m); [n, "reached"] }
fn decl_string(x: int | error) { let s: string = string(x); [s, "reached"] }
fn decl_ord(m: string | error) { let o: int? = ord(m); [o, "reached"] }
fn decl_index(m: string | error) { let i: int? = index_of(m, "b"); [i, "reached"] }
fn decl_split(m: string | error) { let p: string[] = split(m, ","); [len(p), "reached"] }
fn free_count(m: string | error) { let c = count(m); [c, "reached"] }
fn free_symbol(m: string | error) { let y = symbol(m); [y, "reached"] }
fn free_normalize(m: string | error) { let z = normalize(m); [z, "reached"] }
// through a nested call, and from a contained defect
fn nested(m: string | error) { len(string(m)) + 1 }
fn defect_arg(s) { let n = len(string(chk(dyn(s)))); [n, "reached"] }
// a handler and a test see the original error
fn handled(m: string | error) { len(m) ^ { 0 - 1 } }
fn msg(m: string | error) { let v = len(m); if (v is error) v.message else v }
pn assigned(m: string | error) {
    var n: int = 0
    n = len(m);
    [n, "reached"]
}
pn main() {
    print([decl_len("ab"), decl_len(error("e1"))])
    print("\n")
    print([decl_string(7), decl_string(error("e2"))])
    print("\n")
    print([decl_ord("a"), decl_ord(error("e3"))])
    print("\n")
    print([decl_index("abc"), decl_index(error("e4"))])
    print("\n")
    print([decl_split("a,b"), decl_split(error("e5"))])
    print("\n")
    print([free_count("ab"), free_count(error("e6"))])
    print("\n")
    print([free_symbol("ab"), free_symbol(error("e7"))])
    print("\n")
    print([free_normalize("ab"), free_normalize(error("e8"))])
    print("\n")
    print([nested("abc"), nested(error("e9"))])
    print("\n")
    print([defect_arg(21), defect_arg("x")])
    print("\n")
    print([handled("abcd"), handled(error("e10"))])
    print("\n")
    print([msg("abcde"), msg(error("e11"))])
    print("\n")
    print([assigned("abc"), assigned(error("e12"))])
    print("\n")
}
