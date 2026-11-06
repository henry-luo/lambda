// S8.2.4v3: receiver and coordinates execute once, from left to right.
pn receiver(var order: int) any {
    order = order * 10 + 1
    return reshape([1, 2, 3, 4, 5, 6], [2, 3])
}
pn coordinate(var order: int, mark: int, value: int) int {
    order = order * 10 + mark
    let churn = for (i in 1 to 40) [i, float(i) + 0.25]
    return value
}
pn main() {
    var order = 0
    let result = receiver(order)[coordinate(order, 2, 1), coordinate(order, 3, 2)]
    print([result == 6, order == 123])
}
