// S7.5.2v4: effective failure outcomes never widen the written admission target.
fn risky() int^ => raise error("failure")
pn main() {
    var value: int = 1
    value = risky()
    42
}
