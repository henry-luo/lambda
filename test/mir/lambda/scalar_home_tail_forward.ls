// v3 fixture: a tail edge preserves the pending companion pair.
// A self-recursive function whose result is a pointer-backed 64-bit integer
// resolves the pair only in the receiving activation, after the callee's
// number watermark is restored. Checked by scalar_home_tail_forward.mir-check
// (Stack API #15, #16, #21).

fn accumulate(n: int, acc) { if (n <= 0) acc else accumulate(n - 1, acc + 1i64) }

accumulate(5, 0i64)
