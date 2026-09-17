// Tune29 §19.1 item 2 (D3.2.4v4): a declared T[] read from an admitted
// record's field admits a container without the full check, but an absent
// carrier (an out-of-range element) reads as null and must still be rejected.
type Node = {id: int, edges: int[]}
type Graph = {nodes: Node[]}

pn main() {
    var g: Graph = {nodes: [{id: 0, edges: [1]}]}
    var cs: int[] = g.nodes[3].edges
    print("bound: ")
    print(cs)
}
