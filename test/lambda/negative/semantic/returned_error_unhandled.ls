// S7.4.1v2: returning an error no longer exempts it from acknowledgement.
fn outcome() int | error => error("returned")
pn main() {
    outcome()
    42
}
