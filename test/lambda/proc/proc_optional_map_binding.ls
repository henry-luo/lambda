// A nullable map parameter is a scalar occurrence, never a typed array.
// Its local binding must not call ensure_typed_array(map, map).

pn make_chain(length: int) {
    if (length == 0) {
        return null
    }
    return {next: make_chain(length - 1)}
}

pn chain_length(node: map?) int {
    var cursor = node
    var count = 0
    while (cursor != null) {
        count = count + 1
        cursor = cursor.next
    }
    return count
}

pn main() {
    print(chain_length(make_chain(3)))
    print("\n")
}
