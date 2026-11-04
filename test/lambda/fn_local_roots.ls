// D5.3.2/S9.1.2: allocating loops preserve input copies and escaping captures.
fn build(n) {
    var work = {total: 0, last: [0, 0]}
    var i = 0
    while (i < n) {
        let row = [i, i + 1]
        work = {total: work.total + row[0], last: row}
        i = i + 1
    }
    let snapshot = work
    let read = () => snapshot.total
    work.total = -1
    return [read(), snapshot.last, work.total]
}

fn recover() int^ {
    var i = 0
    while (i < 3) {
        let converted = int("bad") ^ { return 7 }
        i = i + 1
    }
    return 0
}

fn iterations(n: int) int {
    var i = 0
    while (i < n) { i = i + 1 }
    return i
}

{built: build(200), repeated: [for (i in 1 to 8) iterations(20)], recovered: recover()^}
