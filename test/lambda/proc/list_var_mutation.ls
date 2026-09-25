// P0 fixture of vibe/impl/Lambda_List_Fixes (done).md. It moved from
// test/lambda/proc-ext to the baseline once green; the last blocker was the
// no-op push onto an open numeric array (LR12-27).
// S2.5.5v2 / S2.5.6 / S2.5.7 in procedures: the kind survives mutation of a
// `var` list; a collapsed empty list is null and pushes as null (an argument
// is a value position); a list value inserted into an array splices; a field
// stores the array image.

pn main() {
    var l = (1, 2)
    push(l, 3)
    print(type(l)); print(" "); print([l, 9]); print("\n")
    l[0] = 7
    print([l, 9]); print("\n")
    var a = [1]
    push(a, (5, 6))
    print(a); print("\n")
    let e = for (x in []) x
    push(a, e)
    print(a); print(" "); print(len(a)); print("\n")
    push(a, for (x in []) x)
    print(len(a)); print("\n")
    var m = {f: (1, 2)}
    m.f = (3, 4)
    print(type(m.f)); print(" "); print(m.f); print("\n")
    var b = [1, 2]
    b[0] = (8, 9)
    print(b); print("\n")
}
