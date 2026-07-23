// A shallow element detach must retain COW ownership for nested child edits.

pn edit_first(var doc: element) {
    doc[0][0][0] = "published"
    doc[0][0].revision = 1
}

pn main() {
    var rows = []
    push(rows, <p revision: 0; "draft">)
    var doc = <article; rows>
    var snapshot = doc
    edit_first(doc)
    print(snapshot[0][0][0])
    print(" ")
    print(snapshot[0][0].revision)
    print(" ")
    print(doc[0][0][0])
    print(" ")
    print(doc[0][0].revision)
    print("\n")
}
