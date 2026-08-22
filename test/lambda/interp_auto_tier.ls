// P2: a closed definition begins in T0 and crosses into its satellite on the
// third dynamic entry. The recursive edge then exercises T0 -> T1 -> T1.
fn count_down(n: int) int =>
    if (n <= 0) 0
    else 1 + count_down(n - 1)

fn sum_down(n: int) int =>
    if (n <= 0) 0
    else n + sum_down(n - 1)

let offset = 2

fn shifted(n: int) int => n + offset
fn bridge(n: int) int => shifted(n) + offset

// P2 backedge marking is deliberately deferred to a later entry: the first
// call crosses the tiny comprehension-loop budget in T0, and the second must
// enter its satellite even while the ordinary call threshold is unreachable.
fn loop_count(limit: int) => len([for (i in 0 to limit - 1) i]);

[count_down(3), count_down(4), count_down(5), sum_down(3), sum_down(4),
    bridge(1), bridge(2), bridge(3), loop_count(4), loop_count(2)]
