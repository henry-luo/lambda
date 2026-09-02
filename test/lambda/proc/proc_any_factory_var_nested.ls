// An explicit any return must not change root-var or nested-var mutation.
pn sparse_new() {
    return { slots: fill(2, null) }
}

pn state_new() any {
    return { sparse: sparse_new(), count: 0 }
}

pn sparse_set(var sparse, index, value) {
    sparse.slots[index] = value
}

pn state_add(var holder, value) {
    sparse_set(holder.sparse, 0, value)
    holder.count = holder.count + 1
}

pn state_add_mid(var holder, value) {
    state_add(holder, value)
}

pn state_add_outer(var holder, value) {
    state_add_mid(holder, value)
}

pn main() {
    var holder = state_new()
    state_add_outer(holder, 7)
    var prior = holder
    state_add(holder, 8)
    print(prior.count)
    print(" ")
    print(prior.sparse.slots[0])
    print(" ")
    print(holder.count)
    print(" ")
    print(holder.sparse.slots[0])
    print("\n")
}
