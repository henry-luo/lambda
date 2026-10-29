fn dynamic(x) => x
fn broken() => { let n: int = dynamic("bad"); n }
let failure = broken();
[failure is error, failure.line == 2, failure.column > 0, contains(failure.file, "ledger_boundary_location.ls")]
