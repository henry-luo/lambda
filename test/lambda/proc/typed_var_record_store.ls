// JIT fix (2026-09-07): a nested store through a TYPED record `var` parameter
// (`m.values[i] = v`, `m.inner.k = v`, `m.inner[key] = v`) took the detaching
// checked helper and left the caller's record untouched -- a typed `var` root
// has no home to publish the swapped-in candidate through. Both emitter sites
// now select the NM-O8 typed arm (lambda_map_path_set_checked_inplace), which
// validates and coerces on a candidate and lands the coerced leaf in place:
// a rejected write leaves the record untouched, an int into a float field is
// converted, and the caller observes every accepted write.
fn dynamic(value) any { value }
type Inner = {k: int, w: float, tag: string}
type Rec = {values: array, inner: Inner, n: int}
pn put_direct(var r: Rec, i: int, v: int) any { r.values[i] = v }
pn put_key(var r: Rec, key: string, v: int) any { r.inner[key] = v }
pn put_deep(var r: Rec, v: int) any { r.inner.k = v; r.inner.tag = "deep" }
pn put_float(var r: Rec, v: int) any { r.inner.w = v }          // int -> float field: coerced
pn put_bad(var r: Rec) Rec^ { r.inner.k = dynamic(2.5) }        // rejected: record untouched
pn main() {
    var r: Rec = {values: fill(8, 0), inner: {k: 0, w: 0.0, tag: ""}, n: 8}
    put_direct(r, 5, -5)
    put_deep(r, 41)
    put_key(r, "k", 42)
    put_float(r, 3)
    var failed = null
    put_bad(r) ^ { failed = "rejected" }
    print(r.values[5], " ", r.inner.k, " ", r.inner.tag, " ", r.inner.w, " ", r.inner.w is float, " ", failed, "\n")
}
