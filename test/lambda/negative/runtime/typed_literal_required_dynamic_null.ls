// T29-2 (D3.2.6): a dynamic null reaching a required scalar field of a
// contract-constructed literal is rejected on every tier.
type Cell = {k: int, name: string}

pn nothing() any { return null }

pn main() {
    var c: Cell = {k: nothing(), name: "x"}
    print("bound: ")
    print(c.k)
}
