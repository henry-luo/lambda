// S7.5.1v2 (LR10-16): parameter error admission follows names, not source order.
fn risky(n: int) int^ { if (n < 0) { raise error("negative") } else { n } }
fn receive_last(a: int, b: int | error) => [a, b is error]
fn receive_first(a: int | error, b: int) => [a is error, b];
[receive_last(b: risky(-1), a: 2), receive_first(b: 2, a: risky(-1)), receive_last(a: 2, b: 3)]
