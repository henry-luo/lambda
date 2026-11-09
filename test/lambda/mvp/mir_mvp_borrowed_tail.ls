// D5.4.6/D6.3: a self-tail loop retains its var homes and precise roots.
pn drain(var owner, remaining: int, delta: int) any {
    if (remaining == 0) { return owner.total }
    let churn = fill(4, remaining)
    owner.total = owner.total + delta
    return drain(owner, remaining - 1, delta)
}
pn fail_last(var owner, remaining: int) any {
    if (remaining == 0) { return error({code: 7010, message: "tail kept"}) }
    owner.total = owner.total + 1
    return fail_last(owner, remaining - 1)
}
pn cold(var owner: any, remaining: int) any {
    if (remaining == 0) { return owner }
    let churn = fill(4, remaining)
    owner = if (remaining % 2 == 0) -inf else inf
    return cold(owner, remaining - 1)
}
pn child_tail(var owner, remaining: int) any {
    if (remaining == 0) { return owner.value }
    var child = owner.child
    // a different owner retains its ordinary borrow/write-back call.
    return child_tail(child, remaining - 1)
}
pn main() {
    var owner = {total: 0}
    let saved = owner
    let answer = drain(owner, 200, 3)
    print([answer, owner.total, saved.total]); print("\n")
    let failure: any = fail_last(owner, 5)
    print([failure.message, owner.total, saved.total]); print("\n")
    var payload: any = null
    let nonfinite = cold(payload, 200)
    print([nonfinite == inf, payload == inf]); print("\n")
    var tree = {child: {child: {value: 17}}}
    print([child_tail(tree, 2), tree.child.child.value]); print("\n")
}
