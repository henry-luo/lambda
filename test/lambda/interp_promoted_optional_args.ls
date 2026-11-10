// D8.3.3: a promoted caller must pass omission markers through resident calls.
fn target(a, b, c, d, flag = false, limit = 100000) any => [a, b, c, d, flag, limit]
fn dispatch(n: int) any => target(n, {key: n}, "text", null);
all([for (i in 0 to 20) dispatch(i) == [i, {key: i}, "text", null, false, 100000]])
