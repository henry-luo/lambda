// D6.1.1/S12.1.2v3: a transfer retires every abandoned traversal frame.
pn first(rows) {
    for (left in rows, right in {a:10, b:20}) {
        if (left > 0) { return left + right }
    }
    return -1
}
pn stopped(rows) {
    var seen = 0
    for (left in rows, right in {a:10, b:20}) {
        seen = seen + 1
        break
    }
    return seen
}
pn skipped(rows) {
    var seen = 0
    for (left in rows, right in {a:10, b:20}) {
        if (right == 10) { continue }
        seen = seen + left + right
    }
    return seen
}
fn checked(value: int) int => value
pn mapped(rows) int^ {
    let answer = rows |> checked(~)^
    return 0
}
pn advance(var value) {
    value = value + 1
    return value
}
pn changed() {
    var rows = [1, 2]
    let answers = [0, 1] |> advance(rows[~])
    return [rows, answers]
}
pn main() {
    print(first({a:1, b:2}) == 11)
    print(stopped({a:1, b:2}) == 1)
    print(skipped({a:1, b:2}) == 43)
    print(mapped({a:1, b:"bad"}) is error)
    print(changed() == [[2, 3], [2, 3]])
    print(first({a:2}) == 12)
}
