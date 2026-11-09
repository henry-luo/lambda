// S9.1.3v3: later argument effects update a live borrow, not its old owner.
pn extend(var owner) int^ {
    let before = owner
    owner.items = owner.items ++ [20]
    return len(before.items)
}
pn record(var owner, previous_count, options = null) any {
    owner.previous = previous_count
}
pn nested(var owner) any^ { record(owner, extend(owner)^)^ }
pn main() {
    var owner = {items: [10], previous: -1}
    let snapshot = owner
    nested(owner)^
    print([owner.items, owner.previous, snapshot.items]); print("\n")
    var parent = {child: {items: [30], previous: -1}}
    let saved = parent
    record(parent.child, extend(parent.child)^)^
    print([parent.child.items, parent.child.previous, saved.child.items]); print("\n")
}
