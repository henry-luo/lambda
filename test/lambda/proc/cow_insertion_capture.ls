// S9.3.1 insertion capture — DEFAULT ON since the 2026-08-29 flip (LR12-9).
// Placing a NAMED value into a container captures it by value at every
// ruled point: array element store, array literal, map field store, map
// literal. Each probe's mutation-after-insertion must not reach the copy.
pn main() {
    // probe 1: array element store
    var t = {n: 1}
    var arr: any[] = [0]
    arr[0] = t
    t.n = 55
    print("p1 " ++ (arr[0].n) ++ " (ruled 1)\n")

    // probe 2: array literal
    var u = {n: 1}
    var lit = [u]
    u.n = 66
    print("p2 " ++ (lit[0].n) ++ " (ruled 1)\n")

    // probe 3: map field store
    var a = {peer: null}
    var b = {n: 1}
    a.peer = b
    b.n = 99
    print("p3 " ++ (a.peer.n) ++ " (ruled 1)\n")

    // probe 4: map literal
    var c = {n: 1}
    var d = {peer: c}
    c.n = 77
    print("p4 " ++ (d.peer.n) ++ " (ruled 1)\n")

    // binding copy (S9.1.2) must still hold
    var g = {n: 1}
    var h = g
    h.n = 5
    print("p5 " ++ (g.n) ++ " (ruled 1)\n")
}
