// MT5 fixture, revived from the orphaned
// test/lambda/proc/tail_call_proc.transpile. Procedural (pn) tail recursion
// must get the same counter-guarded loop as the functional form, so the
// procedural lowering path is pinned separately from tail_call_tco.ls.
// Checked by tail_call_tco_proc.mir-check.

pn countdown(n: int, acc: int) int { if (n <= 1) acc else countdown(n - 1, acc * n) }

pn main() { countdown(10, 1) }
