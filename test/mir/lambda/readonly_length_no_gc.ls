// D5.3.1: both live pointer arguments cross only non-collecting length reads.
fn lens(a: string[] as W, b: string) int => len(a) + len(b) + len(a)
fn shaped(a: float[][] as M) int => len(a)

let readings = [lens(split("a,b", ","), "xyz"),
    shaped([[1.0, 2.0], [3.0, 4.0]]), shaped([[1.0, 2.0]])]
readings
