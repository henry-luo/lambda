// @expect-error: E228
fn risky() int^ { raise error("negative") }
fn f(a: int | error, b: int) => b
f(b: risky(), a: 2)
