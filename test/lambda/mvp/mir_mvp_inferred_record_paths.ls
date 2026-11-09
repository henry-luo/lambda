// D7.4.1/S9.1.2: inferred paths guard every owner and keep ordinary retags.
fn child(initial: int) any => {value: initial}
fn world(initial: int) any => {children: [child(initial)]}
pn replace(var owner, slot: int, incoming: any) any {
    owner.children[slot].value = incoming
    return owner
}
pn observe_then_write(var owner) any {
    var row = owner.children[0]
    let before = row.value
    owner.children[0].value = before + 1
    return before
}
pn observe_while_write(var owner) any {
    var row = owner.children[0]
    owner.children[0].value = 30
    return row.value
}
pn keep_observer(var owner) any {
    let row = owner.children[0]
    let saved = () => row.value
    owner.children[0].value = 40
    return saved
}
pn main() {
    var owner = world(1)
    replace(owner, 0, 4)^
    print(owner.children[0].value); print("\n")
    let saved = owner
    replace(owner, 0, 7)^
    print([owner.children[0].value, saved.children[0].value]); print("\n")
    replace(owner, 0, 2.5)^
    print([owner.children[0].value == 2.5, saved.children[0].value]); print("\n")
    replace(owner, 0, 9)^
    print([owner.children[0].value == 9, saved.children[0].value]); print("\n")
    // unknown array children must retain tags before the next map guard.
    var invalid: any = {children: ["0123456789abcdefg"]}
    let failure: any = replace(invalid, 0, 3)
    print(failure is error); print("\n")
    // D5.4.5: only the first projection dies before the source write.
    var stable = world(10)
    print([observe_then_write(stable), stable.children[0].value]); print("\n")
    print([observe_while_write(stable), stable.children[0].value]); print("\n")
    let kept = keep_observer(stable)
    print([kept(), stable.children[0].value]); print("\n")
}
