// T29-2 (D3.2.6, S11.4.10): a required field is a required field. A record
// literal adopting a declared contract must reject null in a required array
// field. The JIT's shaped-literal path used to accept it and store a zero word
// in the slot, because the field-proof test mistook the declaration wrapper of
// a plain field for an optional one.
type Cell = {k: int}
type Grid = {cells: Cell?[], size: int}

pn main() {
    var g: Grid = {cells: null, size: 1}
    print("bound: ")
    print(g.cells)
}
