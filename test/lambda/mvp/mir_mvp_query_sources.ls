// S9.2.3/S14.1: dependent inner sources retain row order and source snapshots.
fn query_pairs() any { for (x in [1, 2], y in [x, x + 10]) [x, y] }
pn main() {
    print([query_pairs()]); print("\n")
    print([for (x in 1 to 2, y in 1 to 3, let total = x * 10 + y where x + y > 3
        order by total desc limit 2 offset 1) total]); print("\n")
    print([for (x in 1 to 2, k, v in {a: x, b: x + 1}) [k is symbol, v]]); print("\n")
    print([for (x in [1, 2], character in "aé") character]); print("\n")
    print([for (x in [1, 2], y in []) [x, y]]); print("\n")
    print([for (x in [], y in [1, 2]) [x, y]]); print("\n")
    var total: int = 0
    for (x in 1 to 2, y in 1 to 3) { total = total + x * y }
    print(total); print("\n")
}
