// Tune27 T27-8 / T27-9: a value that already carries a contract does not cross
// it again (S11.4.1v3, D3.2.2, D3.2.4v3). Declared bindings and record fields
// prove their own contract through the optional wrapper; `null` proves `T?`;
// a non-raising call proves its declared return; a float literal under a
// float[] contract admits only its null-member arm; a constant index into a
// known-length local never takes the out-of-range null arm (S7.1.3v2).
type N = {key: float, left: N?}
type T = {root: N?}

pn ret_param(var n: N?) N? { return n }
pn ret_null(k: int) N? { if (k < 0) { return null } return null }
pn read_field(t: T) N? { var x: N? = t.root; return x }
pn alias_param(t: T) T { var u = t; return u }
pn twice(m: float[]) float[] { return [m[0] * 2.0, m[1] * 2.0] }
pn decl_from_call(m: float[]) float { var nv: float[] = twice(m); return nv[0] + nv[1] }
pn take(v: float[]) float { return v[0] + v[1] }
pn literal_arg(q: float[], i: int) float { return take([q[i], q[i + 1]]) }
pn const_index() float { var v: float[] = fill(4, 0.0); v[1] = 2.5; return v[1] + v[3] }
// the push makes the length dynamic again: the read keeps its null arm
pn const_index_pushed() float { var v: float[] = fill(4, 0.0); push(v, 1.0); return v[4] }

pn main() {
    var t: T = {root: {key: 1.5, left: null}}
    var q: float[] = [1.0, 2.0, 3.0]
    print([ret_param(t.root), ret_null(1), read_field(t), alias_param(t)])
    print("\n")
    print([decl_from_call(q), literal_arg(q, 0), literal_arg(q, 1), type(literal_arg(q, 7))])
    print("\n")
    print([const_index(), const_index_pushed()])
    print("\n")
}
