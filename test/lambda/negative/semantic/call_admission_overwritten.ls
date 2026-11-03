// S7.7.3/S7.5.5: an implicit argument check can produce an ordinary error.
fn opaque(value) => value
fn checked(value: int) int => value
pn main() {
    checked(opaque("bad"))
    42
}
