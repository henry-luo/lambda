// S2.5.3v3/S2.5.5v2: statement-only fn blocks need no list allocation.
fn spin(limit: int) int {
    var count: int = 0
    while (count < limit) { count = count + 1 }
    return count
}
fn shifted(seed) {
    var value = seed
    var count = 0
    while (value > 1) { value = shr(value, 1); count = count + 1 }
    return count
}
[spin(20000) == 20000, shifted(1024) == 10]
