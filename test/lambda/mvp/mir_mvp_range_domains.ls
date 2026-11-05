// S11.1.3/S7.7.3: range members admit clean calls even with dynamic bounds.
fn singleton(value) => [value]
fn numbers(bound) any^ => [for (i in (bound - 1) to (bound + 1))
    for (value in singleton(i)) value]
fn letters(lo, hi) any^ => [for (i in lo to hi)
    for (value in singleton(i)) value]
fn failed_range() any^ {
    let broken = error("bad bound") to 3;
    [for (value in broken^) value]
}
[numbers(2), letters("a", "c"), failed_range() ^ { ^.message }]
