// D8.1.1v14/S12.1.2v3: hot fn loops retain functional completion and colour.
fn total(n: int) float {
    var i: int = 0
    var sum: float = 0.0
    while (i < n) { sum = sum + float(i); i = i + 1 }
    return sum
}
fn collected(n: int) {
    var i: int = 0
    var sum: float = 0.0
    while (i < n) { sum = sum + float(i); i = i + 1 }
    sum
    sum + 1.0
}
fn prefix(n: int) {
    7
    var i: int = 0
    var sum: float = 0.0
    while (i < n) { sum = sum + float(i); i = i + 1 }
    sum
}
fn replaced(n: int) float {
    7
    var i: int = 0
    var sum: float = 0.0
    while (i < n) { sum = sum + float(i); i = i + 1 }
    return sum
}
// a resumed loop owns its COW locals without changing its caller's snapshot.
fn detached(source, n: int) {
    var values = source
    var i: int = 0
    while (i < n) {
        values[0] = i
        if (i == 15000) { return values[0] }
        i = i + 1
    }
    return -1
}
let original = [3, 4]
let pair = collected(20000)
let saved = prefix(20000);
[total(20000) == 199990000.0, pair[0] == 199990000.0, pair[1] == 199990001.0,
 saved[0] == 7, saved[1] == 199990000.0, replaced(20000) == 199990000.0,
 detached(original, 20000) == 15000, original[0] == 3]
