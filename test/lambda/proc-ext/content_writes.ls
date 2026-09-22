// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/proc-ext until
// phase P3 turns it green, then moves to test/lambda/proc (baseline).
// S2.6.5: content stays normalized under mutation — writing null or "" removes
// the child, a written list splices, a string beside a string merges, and
// removing a separator merges its neighbours; `push` on an element inserts
// content.

pn main() {
    var e = <e "x" 1 "y">
    e[1] = "m"
    print(len(content(e))); print(" "); print(content(e)[0]); print("\n")
    var f = <e "x" 1 "y">
    f[1] = null
    print(len(content(f))); print(" "); print(content(f)[0]); print("\n")
    var g = <e 1 2>
    g[0] = ""
    print(len(content(g))); print(" "); print(content(g)[0]); print("\n")
    var h = <e 1 2>
    h[0] = (3, 4)
    print(len(content(h))); print(" "); print(content(h)); print("\n")
    var k = <e "x">
    push(k, "y")
    print(len(content(k))); print(" "); print(content(k)[0]); print("\n")
    var n = <e "x">
    push(n, null)
    print(len(content(n))); print("\n")
    var p = <e "a" 1 "b">
    p[1] = "-"
    print(len(content(p))); print(" "); print(content(p)[0]); print("\n")
}
