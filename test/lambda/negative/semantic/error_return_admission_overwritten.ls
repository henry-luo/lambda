// S7.5.5: deferred return admission is an ordinary outcome at its receiver.
fn opaque(value) => value
fn checked(value) float[] => opaque(value)
pn main() {
    checked("bad")
    42
}
