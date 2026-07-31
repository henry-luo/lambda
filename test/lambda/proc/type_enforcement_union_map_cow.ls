type Entry = {id: string | error}

pn main() {
    var entry: Entry = {id: string("a")}
    let snapshot = entry
    entry.id = string("b")
    print(snapshot.id)
    print(" ")
    print(entry.id)
}
