// S11.1.6v2: `T?` is `T | null`, and S1.6: the carrier a tier picks for it is
// invisible. The JIT keeps a nullable bool in its 0/1/2 lane and a nullable
// pointer as the raw pointer, zero for null (D2.5.1). Two binding paths broke
// that on the JIT only:
// - a `bool?` or `string?` call result published the TypeId `any` beside its
//   unboxed lane, so `let` stored the raw lane as an Item: printing a bound
//   `bool?` crashed, and a bound non-null `string?` read as null;
// - a local pointer binding holds its boxed Item while its reads keep the
//   pointer descriptor, and boxing that ItemNull again tagged it as a string
//   or symbol (""), a decimal (error) or a binary (crash), or dereferenced it
//   as a datetime (crash).

import .mod_nullable_lane_bindings

type Row = {value: string?}

fn flag(x: int) bool? { if (x > 0) true else if (x < 0) false else null }
fn text(x: int) string? { if (x > 0) "hi" else null }
fn dyn(v) any { v }
fn rebind(s: string?) { let t = s; [t, t == null] }

pn main() {
    // `bool?` call results bound by `let` and `var`, local and imported
    let a = flag(1)
    let b = flag(-1)
    let c = flag(0)
    var d = flag(0)
    let e = remote_flag(1)
    let f = remote_flag(0)
    print(a)
    print("\n")
    print([a, b, c, d, e, f, type(c), type(e)])
    print("\n")
    print([if (c) "y" else "n", a and true, c == null, f == null])
    print("\n")

    // `string?` call results
    let s = text(1)
    let t = text(0)
    var u = text(0)
    let v = remote_text(1)
    let w = remote_text(0)
    print([s, t, u, v, w, len(s), t == null, type(s), type(u)])
    print("\n")

    // a null `string?` field read bound to a `var`
    var source = {value: "left"}
    var target: Row = source
    target.value = dyn(null)
    var blank = target.value
    print([blank, blank == null, target])
    print("\n")

    // null string lanes through declarations, rebinding, and consumers
    var n: string? = null
    let m = n
    var k = "a"
    k = n
    print([n, m, k, m == null, type(m), m is string, m ++ "x", rebind(null), rebind("r")])
    print("\n")

    // the other pointer lanes: symbol, decimal, binary, datetime
    let y: symbol? = null
    let y2 = y
    let dc: decimal? = null
    let dc2 = dc
    let bn: binary? = null
    let bn2 = bn
    let dt: datetime? = null
    let dt2 = dt
    print([y, y2, dc, dc2, bn, bn2, dt, dt2, y2 == null, dc2 == null, dt2 == null])
    print("\n")

    // non-null values keep their own tag through the same bindings
    let y3: symbol? = 'sym'
    let y4 = y3
    let dc3: decimal? = 1.5m
    let dc4 = dc3
    let bn3: binary? = b'\xDEAD'
    let bn4 = bn3
    let dt3: datetime? = t'2025-01-02'
    let dt4 = dt3
    print([y4, dc4 + 1m, bn4, len(bn4), dt4, dt4.year, y4 == 'sym'])
    print("\n")
}
