// @expect-error: E312
fn bad(n: int) int | error { if (n > 0) { raise error("x") } else { n } }
bad(0)
