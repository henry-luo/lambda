// S7.6.9: a narrow filter cannot silently discard unmatched errors.
type Other = error that ~.message == "other"
fn risky() int^ => raise error("first")
pn main() {
    risky() ^Other { 0 }
    42
}
