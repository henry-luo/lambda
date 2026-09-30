// D4.4.4v4 / S9.1.2: a typed path may store an aligned leaf of a record
// containing a packed bool; an unaligned leaf keeps the checked COW path.
type Entry = {id: int, ready: bool, count: int}
type Store = {entries: Entry?[]}

pn update(var store: Store, index: int) int {
    store.entries[index].id = 7
    store.entries[index].ready = true
    store.entries[index].count = 9
    return store.entries[index].id + store.entries[index].count
}

pn main() {
    var store: Store = {entries: [{id: 1, ready: false, count: 2}]}
    let old = store
    print(update(store, 0))
    print(" ")
    print([store.entries[0].id, store.entries[0].ready,
           store.entries[0].count])
    print(" ")
    print([old.entries[0].id, old.entries[0].ready,
           old.entries[0].count])
    print("\n")
}
