// ER-S3: `pn` error destructuring retains ordinary ItemError behavior while
// the MIR body owns a local system-fault checkpoint around each RHS.
fn fail() int^ {
    raise error("ordinary failure")
}

pn main() {
    let value^err = fail()
    let success^success_err = 7
    print([value, ^err, type(err), success, ^success_err])
}
