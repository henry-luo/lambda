// S9.1.3v4: a borrow reads its current home after later actuals finish.
fn observe(box) int^ => len(box.items)
pn append_first(var box) int^ {
    let count = observe(box)^
    push(box.items, count + 7)^
    return 3
}
pn append_next(var box, value: int) int^ {
    push(box.items, value)^
    return len(box.items)
}
pn outer(var box) int^ {
    return append_next(box, append_first(box)^)^
}
pn main() {
    var box = {items: []}
    let count = outer(box)^
    print([box.items, count])
}
