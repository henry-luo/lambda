// Two MIR Direct divergences from T0, found while probing CW34 (2026-09-07):
// (1) S16.4.1v3 / S12.1.2: a pn body whose tail is an `if` with braced arms
//     (or a `null` arm and an index-read arm) is the body's value; the
//     proc-mode branch lowering discarded it and returned null.
// (2) S7.4.1 / S7.4.5: a failed push/splice is a soft error VALUE that the
//     statement discards; the COW push arm republished the error Item as the
//     binding, so `t.vals = vals` stored it and `len(vals)` handed it to the
//     native return lane (`inf`).
pn braced(a) { let c1 = a.l0[1]; if (c1 == null) { 1 } else { 2 } }
pn null_arm(a, idx) { let c1 = a.l0[idx div 16]; if (c1 == null) null else c1[idx % 16] }
pn zero_arm(a, idx) { let c1 = a.l0[idx div 16]; if (c1 == null) 0 else c1[idx % 16] }
pn whole(a) { let c1 = a.l0[1]; if (c1 == null) null else c1 }
pn nested_tail(a, flag) {
    let c1 = a.l0[1]
    if (flag) { if (c1 == null) { "none" } else { "some" } } else { "flag off" }
}
pn push_then_len(var t, key) {
    var vals = t.vals
    if (key == 1) { vals[0] = 100; t.vals = vals; return 100 }
    push(vals, key)          // t.vals is a numeric array: push fails softly
    t.vals = vals
    return len(vals)
}
pn splice_then_len(var t) {
    var vals = t.vals
    splice(vals, 0, 1)       // splice on a numeric array succeeds in place
    t.vals = vals
    return len(vals)
}
pn main() {
    var a = { l0: [null, fill(16, 7)] }
    print(braced(a), " ", null_arm(a, 21), " ", null_arm(a, 5), " ", zero_arm(a, 21), " ", zero_arm(a, 5), "\n")
    print(len(whole(a)), " ", nested_tail(a, true), " ", nested_tail(a, false), "\n")
    var t = {vals: [5]}
    let r1 = push_then_len(t, 1)
    let r2 = push_then_len(t, 2)
    print(r1, " ", r2, " ", t.vals, " ", type(r2), "\n")
    var u = {vals: [1, 2, 3]}
    print(splice_then_len(u), " ", u.vals, "\n")
}
