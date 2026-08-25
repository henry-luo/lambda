// Tune19 §11.5 recursive-record adoption: a self-referential map contract
// must pass mir_map_contract_storage_valid, so returning the literal under
// the declared contract lowers to direct allocation + field stores.
type Node = {val: int, next: Node?}

pn build(v: int, rest: Node?) Node {
    return {val: v, next: rest}
}

pn total(n: Node?) int {
    if (n == null) { return 0 }
    return n.val + total(n.next)
}

pn main() {
    var head: Node? = null
    for (i in 1 to 5) {
        head = build(i, head)
    }
    print(total(head))
}
