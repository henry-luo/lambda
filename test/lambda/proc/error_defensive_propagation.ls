// S7.6.3v3: inferred cleanness permits a defensive caret with a warning.
fn clean() => 7
pn main() {
    let value = 7
    let defended = value^
    let called = clean()^
    let classified = (defended == 7)^;
    [defended, called, classified]
}
