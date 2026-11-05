// S9.1.2/S9.1.3v3: an exclusive place borrow detaches aliases and publishes replacements.
pn slot_set(var slots, index: int, value: int) { slots[index] = value }
pn replace_slots(var slots) { slots = [9, 10] }
pn scalar_set(var value) { value = 17 }
pn shared_argument(var slots, snapshot) {
    slots[0] = 20
    return snapshot[0]
}
pn update_and_fail(var slots) {
    slots[1] = 44
    return shl(1, -1)
}

pn main() {
    var owner = {slots: fill(4, 0), rows: [[1, 2]], scalar: 5}
    let snapshot = owner
    slot_set(owner.slots, 1, 7)^
    slot_set(owner.rows[0], 0, 8)^
    scalar_set(owner.scalar)^
    print([owner.slots, owner.rows, owner.scalar]); print("\n")
    print([snapshot.slots, snapshot.rows, snapshot.scalar]); print("\n")
    let old_slots = owner.slots
    replace_slots(owner.slots)^
    print([owner.slots, old_slots]); print("\n")
    let observed = shared_argument(owner.slots, owner.slots)
    print([observed, owner.slots]); print("\n")
    let failed = update_and_fail(owner.slots)
    print([failed is error, owner.slots]); print("\n")
}
