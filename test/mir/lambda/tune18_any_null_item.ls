// Tune18 regression: an erased `any` value must not use the raw ItemNull
// comparison unless its runtime carrier is proven to be a boxed Item.

pn make_any_chain18(length: int) any {
    if (length == 0) {
        return null
    }
    return {key: length, next: make_any_chain18(length - 1)}
}

pn walk_any_chain18(node: any) int {
    var cursor = node
    var count = 0
    while (cursor != null) {
        var key: int = cursor.key
        count = count + key
        cursor = cursor.next
    }
    return count
}

pn main() {
    print(walk_any_chain18(make_any_chain18(3)))
    print("\n")
}
