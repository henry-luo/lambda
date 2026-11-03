// @expect-error: E228
// S7.4.2v3: raising under a union is valid; the bare caller must acknowledge.
fn bad(n: int) int | error { if (n > 0) { raise error("x") } else { n } }
bad(0)
