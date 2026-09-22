// Record-contract boundaries (D3.2.4v4): an exact shape or an allowed null is
// admitted inline; any other value takes the runtime check, which converts a
// compatible map and rejects a mismatch. A tail map literal is built in its
// record contract like `return {...}` and admits each field; an uncertified
// `Node[]` is admitted through a one-level copy (D4.4.2).
type Node = {kind: int, label: string, kids: Node[], next: Node?}
let no_kids: Node[] = []

fn leaf(kind: any, label: any) Node => {kind: kind, label: label, kids: no_kids, next: null}

fn weight(n: Node?) int {
    if (n == null) 0
    else n.kind + weight(n.next)
}

fn kid_count(kids: Node[]) int => len(kids)

pn main() {
    let a = leaf(1, "a")
    let b: Node = {kind: 2, label: "b", kids: [a], next: a}
    print(weight(b)) print(" ") print(weight(null)) print(" ")
    // a map literal that matches Node structurally but was not built as one
    let loose = {kind: 5, label: "loose", kids: no_kids, next: null}
    print(weight(loose)) print(" ")
    let bad_kind = leaf("x", "c") ^ { print("bad-kind ") }
    let bad_label = leaf(3, 7) ^ { print("bad-label ") }
    // `acc ++ [...]` carries no certificate: admission copies one level
    var acc: Node[] = []
    acc = acc ++ [a, b]
    print(kid_count(acc)) print(" ")
    print(acc[1].label) print(" ") print(a.label) print("\n")
}
