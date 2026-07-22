// MT5 fixture: tail edge forwards the incoming caller scalar home.
// A self-recursive function whose result is a pointer-backed 64-bit integer
// must adopt into the home it was given, not into a fresh home of its own
// activation, and must restore the number watermark only after adopting.
// Checked by scalar_home_tail_forward.mir-check (Stack API #15, #16, #21).

fn accumulate(n: int, acc) { if (n <= 0) acc else accumulate(n - 1, acc + 1i64) }

accumulate(5, 0i64)
