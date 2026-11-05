// S9.1.3: a native scalar borrow publishes tagged values on every exit.
pn inline_step(var value: int, amount: int) int {
    value = value + amount
    return value
}
pn boxed_step(var value: int, amount: int) any {
    value = value + amount
    return value
}
pn forward(var value: int) any { return boxed_step(value, 3) }
pn toggle(var flag: bool) bool { flag = not flag; return flag }
fn observed(value: int) int => value
pn partial(var value: int) int^ {
    value = value + 10
    var items: int[] = [1]
    items[9] = 2
    return value
}
pn main() {
    var count: int = 1
    let first = inline_step(count, 2)
    let second = boxed_step(count, 4)
    let third = forward(count)
    print([first == 3, second == 7, third == 10, count == 10]); print("\n")
    var flag: bool = false
    let toggled = toggle(flag)
    print([toggled, flag]); print("\n")
    var counter_map: {count: int} = {count: 2}
    let saved = counter_map
    let changed = boxed_step(counter_map.count, 5)
    print([changed == 7, counter_map.count == 7, saved.count == 2]); print("\n")
    let failed = partial(count)
    print([failed is error, count == 20]); print("\n")
    let direct = observed(1)
    let callback = observed
    print([direct == 1, callback(2) == 2]); print("\n")
}
