// S9.1.2/D5.4.5: retaining a child snapshots that child; whole-root escapes stay owned.
fn selected(owner) any => owner.child
fn wrapped(owner) any => [owner.child, owner.child]
fn original(owner) any => owner
fn saved_reader(owner) any {
    let part = owner.child
    return () => part.value
}
pn local_write(owner) any {
    owner.child.value = 99
    return owner.child.value
}
pn simultaneous(var owner, observer) any {
    owner.child.value = 40
    return observer.child.value
}
pn change(var owner) int {
    owner.child.value = 60
    return 0
}
pn earlier(owner, trailing) any { return owner.child.value }
pn main() {
    var owner = {child: {value: 1}}
    let child = selected(owner)
    owner.child.value = 2
    print([child.value, owner.child.value]); print("\n")
    let children = wrapped(owner)
    let reader = saved_reader(owner)
    owner.child.value = 3
    print([children[0].value, children[1].value, reader(), owner.child.value]); print("\n")
    let image = original(owner)
    owner.child.value = 4
    print([image.child.value, local_write(owner), owner.child.value]); print("\n")
    let observed = simultaneous(owner, owner)
    print([observed, owner.child.value]); print("\n")
    let first = earlier(owner, change(owner))
    print([first, owner.child.value]); print("\n")
}
