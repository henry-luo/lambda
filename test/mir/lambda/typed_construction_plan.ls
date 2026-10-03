// D3.2.4v4, S7.7.2, S9.1.2–S9.1.3: fresh layouts, failure order and snapshots.
type Node = {value: int, next: Node?}

pn bump(var trace: int[], value) {
    trace[0] = trace[0] + 1
    return value
}

pn build(value: int, next: Node?) Node {
    var result: Node = {value: value, next: next}
    return result
}

pn reject(var trace: int[]) Node {
    var result: Node = {value: bump(trace, "bad"), next: bump(trace, null)}
    trace[0] = 99
    return result
}

pn update(var node: Node) {
    node.value = node.value + 1
}

pn main() {
    var trace: int[] = [0]
    var leaf: Node = {value: bump(trace, 5.0), next: bump(trace, null)}
    var node = build(2, leaf)
    var snapshot = node
    update(node)
    print([trace[0], node.value, snapshot.value, node.next.value])
    var reordered: Node = {next: leaf, value: 7.0}
    print([reordered.value, reordered.next.value])
    print(type(reject(trace)))
    print(trace[0])
}
