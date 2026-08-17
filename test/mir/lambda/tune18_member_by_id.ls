// Tune18 E3: nullable unshaped member hops use the raw NameId ABI.

pn make_chain18(length: int) {
    if (length == 0) {
        return null
    }
    return {next: make_chain18(length - 1)}
}

pn walk_chain18(node: map?) int {
    var cursor: map? = node
    var count = 0
    while (cursor != null) {
        count = count + 1
        cursor = cursor.next
    }
    return count
}

pn main() {
    print(walk_chain18(make_chain18(3)))
    print("\n")
}
