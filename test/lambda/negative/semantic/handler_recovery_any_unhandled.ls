// S7.6.1v8: an identity handler cannot soften a broad ordinary-error outcome.
fn outcome() any => error("returned")
pn main() {
    outcome() ^ { ^ }
    42
}
