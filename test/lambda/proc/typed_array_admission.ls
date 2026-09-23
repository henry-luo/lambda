// Tune29 §19.1 item 2 (D3.2.4v4, D3.3.3v3): `[]` crossing a T[] boundary is
// built already admitted, and a declared T[] read from an admitted record's
// field skips the full check for any container value. Every value below must
// behave exactly as through the checked boundary.
type Vec = int[]
type MaybeVec = Vec?
type Node = {id: int, edges: Vec, weights: float[], names: string[]}
type Graph = {nodes: Node[]}

pn ivec() Vec { return [] }
pn fvec() float[] { return [] }
pn bvec() bool[] { return [] }
pn svec() string[] { return [] }
pn mvec() int[][] { return [] }
pn ovec() MaybeVec { return [] }
pn vec_at(var v: Vec, i: int) int { return v[i] }
pn vec_add(var v: Vec, x: int) any { push(v, x) }

pn node_new(id: int) Node {
    var e: Vec = []
    var w: float[] = []
    var n: string[] = []
    return {id: id, edges: e, weights: w, names: n}
}

pn fresh_results() {
    var a = ivec()
    push(a, 3)
    push(a, 4)
    print("ivec=" ++ string(a) ++ " len=" ++ string(len(a)) ++ " at=" ++ string(vec_at(a, 1)) ++ "\n")
    var f = fvec()
    push(f, 1.5)
    print("fvec=" ++ string(f) ++ "\n")
    var b = bvec()
    push(b, true)
    print("bvec=" ++ string(b) ++ "\n")
    var s = svec()
    push(s, "x")
    print("svec=" ++ string(s) ++ "\n")
    var m = mvec()
    push(m, [1, 2])
    print("mvec=" ++ string(m) ++ "\n")
    var o = ovec()
    print("ovec=" ++ string(len(o)) ++ "\n")
    // two calls never share one carrier
    var p = ivec()
    var q = ivec()
    push(p, 1)
    print("fresh=" ++ string(len(p)) ++ "," ++ string(len(q)) ++ "\n")
    var t: Vec = []
    push(t, 7)
    print("decl=" ++ string(t) ++ " type=" ++ string(type(t)) ++ "\n")
}

pn field_reads() {
    var g: Graph = {nodes: [node_new(0), node_new(1)]}
    vec_add(g.nodes[0].edges, 5)
    var cs: Vec = g.nodes[0].edges
    push(cs, 6)
    print("copy=" ++ string(cs) ++ " orig=" ++ string(g.nodes[0].edges) ++ "\n")
    var i: int = 0
    var total = 0
    while (i < len(cs)) {
        total = total + vec_at(cs, i)
        i = i + 1
    }
    print("sum=" ++ string(total) ++ "\n")
    var ws: float[] = g.nodes[1].weights
    push(ws, 2.5)
    print("ws=" ++ string(ws) ++ "\n")
    var n: Node = g.nodes[1]
    var ns: string[] = n.names
    push(ns, "b")
    print("ns=" ++ string(ns) ++ " orig=" ++ string(len(n.names)) ++ "\n")
}

pn main() {
    fresh_results()
    field_reads()
}
