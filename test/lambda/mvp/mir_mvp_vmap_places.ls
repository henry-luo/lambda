// S9.1.3v2/S9.2.2: set writes a place without changing its saved images.
pn main() {
    let base = map(["value", 1])
    var changed = base
    changed.set("value", 9)
    print([base.value, changed.value]); print("\n")

    var outer = {child: map(["value", 2])}
    let saved = outer
    set(outer.child, "value", 8)
    print([saved.child.value, outer.child.value]); print("\n")

    var scalars = [3e-310, 1e-300]
    set(outer.child, "values", scalars)
    scalars[0] = 7
    print([outer.child.values[0] == 3e-310,
           outer.child.values[1] == 1e-300, scalars[0] == 7]); print("\n")

    let before = outer
    var rejected = false
    set(outer.child, 3.5, "invalid") ^ { rejected = ^ is error }
    print([rejected, len(outer.child), before.child.value, outer.child.value]); print("\n")

    var nested = [map(["value", 3])]
    let original = nested
    set(nested[0], "value", 6)
    print([original[0].value, nested[0].value]); print("\n")
}
