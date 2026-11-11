// S9.1.2/D5.4.4: nested typed push detaches shared ancestors and the leaf.
type Bucket = {values: int[]}
type Store = {buckets: Bucket?[]}

pn append(var store: Store, index: int, value: int) any {
    push(store.buckets[index].values, value)^
}

pn main() {
    let initial: Store = {buckets: [{values: [1]}, null]}
    var store: Store = initial^
    let before = store
    append(store, 0, 2)^
    let leaf_before = store.buckets[0].values
    push(store.buckets[0].values, 3)^
    print([before.buckets[0].values, leaf_before, store.buckets[0].values])
    print("\n")
    var bad_null = false
    var bad_index = false
    append(store, 1, 4) ^ { bad_null = true }
    append(store, 2, 4) ^ { bad_index = true }
    print([bad_null, bad_index, store.buckets[0].values])
    print("\n")
}
