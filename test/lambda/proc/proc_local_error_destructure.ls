// ER-S3: a procedural handler retains ordinary ItemError behavior while
// the MIR body owns a local system-fault checkpoint around each RHS.
fn fail() int^ {
    raise error("ordinary failure")
}

pn main() {
    let value = fail() ^ { ^ }
    let success = 7
    print([value, value is error, type(value), success, success is error])
}
