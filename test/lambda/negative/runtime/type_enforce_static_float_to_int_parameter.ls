// A statically-known float never receives implicit boundary admission to int.
// Dynamic any values may be admitted only after the runtime exact-value check.
fn accept_int(value: int) int { value }

pn main() {
    accept_int(3.0)
}
