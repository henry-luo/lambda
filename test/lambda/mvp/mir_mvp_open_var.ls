// S12.2.1v3/S9.1.3: an inferred owner stays open across untyped var write-back.
pn increment(var box) { box.value = box.value + 1 }
pn replace(var box) { box = "changed" }
pn forward(var box) { replace(box) }
pn observe(var box, snapshot) {
    increment(box)
    return snapshot.value
}

pn main() {
    var box = {value: 1}
    increment(box)
    print(box.value); print("\n")
    print(observe(box, box)); print(" "); print(box.value); print("\n")
    forward(box)
    print(box); print("\n")
}
