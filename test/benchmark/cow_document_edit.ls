// COW document gate: build an element tree, retain one snapshot, edit leaves, serialize.

pn build_document(count: int) {
    var rows = []
    var index = 0
    while (index < count) {
        push(rows, <p id: index, revision: 0; "draft">)
        index = index + 1
    }
    return <article class: "draft"; rows>
}

pn edit_document(var doc, rounds: int) {
    var index = 0
    while (index < rounds) {
        var slot = index % len(doc[0])
        doc[0][slot][0] = "published"
        doc[0][slot].revision = index
        index = index + 1
    }
}

pn main() {
    var started = clock()
    var doc = build_document(256)
    var snapshot = doc
    edit_document(doc, 2048)
    var html = format(doc, 'html')
    var elapsed = clock() - started
    print("document-edit:")
    print(len(snapshot[0]))
    print(" ")
    print(snapshot[0][0][0])
    print(" ")
    print(doc[0][0][0])
    print(" ")
    print(doc[0][0].revision)
    print(" ")
    print(len(html))
    print("\n")
    print("__TIMING__:")
    print(elapsed)
    print("\n")
}
