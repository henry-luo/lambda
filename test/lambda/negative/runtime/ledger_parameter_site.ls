fn dynamic(x) => x
fn takes(n: int) => n
let failed = takes(dynamic("s")) ^ { ^ };
[failed is error, failed.line, failed.column]
