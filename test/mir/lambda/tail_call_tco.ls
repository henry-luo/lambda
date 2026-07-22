// MT5 fixture, revived from the orphaned test/lambda/tail_call.transpile.
// That fixture asserted the C2MIR C output ("tco_start:", "goto tco_start",
// "tco_tmp0"); this one asserts the same optimization decision in MIR-Direct,
// where tail recursion becomes a counter-guarded loop instead of a self-call.
// Checked by tail_call_tco.mir-check.

fn factorial(n: int, acc: int) int => if (n <= 1) acc else factorial(n - 1, acc * n)

factorial(10, 1)
