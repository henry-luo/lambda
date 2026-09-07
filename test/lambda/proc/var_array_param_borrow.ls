// T0 fixes (2026-09-07), found by Result37's typed hashmap stall:
// (1) a `var s: array` parameter BORROWS the caller's packed numeric field
//     (S9.1.3); the `any[]` boundary must not hand it a widened copy, which
//     lost the writes and cost O(n) per call;
// (2) the frame plan budgets the scratch homes a CW25 place-borrow argument
//     (`f(hm.values, …)`) holds while its path keys evaluate -- the flat call
//     estimate overflowed ("scratch overflow depth=7 cap=7").
// (the typed-record nested-store shapes live in typed_var_record_store.ls,
//  whose computed-key store T0 does not run yet)
type Rec = {values: array, n: int}
pn slot_set(var s: array, i: int, v: int) any { s[i] = v }
pn put(var r: Rec, i: int, v: int) any { slot_set(r.values, i, v) }
pn widen(var s: array, i: int) any { s[i] = "x" }   // the open contract widens the packed lane in place
pn put_direct(var r: Rec, i: int, v: int) any { r.values[i] = v }
pn main() {
    var r: Rec = {values: fill(64, 0), n: 64}
    var i = 0
    while (i < 64) { put(r, i, i * 2); i = i + 1 }
    widen(r.values, 6)
    put_direct(r, 5, -5)
    print(r.values[0], " ", r.values[63], " ", r.values[5], " ", r.values[6], " ", len(r.values), "\n")
}
