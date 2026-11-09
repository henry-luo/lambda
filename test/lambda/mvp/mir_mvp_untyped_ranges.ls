// D4.3.1 / S8.3.1v3: private bound inference must widen over every caller.
pn total(limit) {
    var sum = 0
    for i in 2 to limit { sum = sum + i }
    return sum
}
pn open_total(limit) {
    var sum = 0
    for i in 2 to limit { sum = sum + i }
    return sum
}
pn changed_limit(limit) {
    var end = limit
    var sum = 0
    end = "bad"
    for i in 2 to end { sum = sum + i }
    return sum
}
pn main() {
    print([total(5), total(1), open_total(5), open_total("bad"), changed_limit(5)])
}
