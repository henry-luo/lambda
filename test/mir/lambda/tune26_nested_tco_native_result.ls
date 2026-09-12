// Tune26 fixture: the inner recursive result is already an IntLane producer.
// The outer tail-call backedge must consume it without an Item round trip.

pn nested_count(m: int, n: int) int {
    if (m == 0) { return n + 1 }
    if (n == 0) { return nested_count(m - 1, 1) }
    return nested_count(m - 1, nested_count(m, n - 1))
}

nested_count(2, 2)
