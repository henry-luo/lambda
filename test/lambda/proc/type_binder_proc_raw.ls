// S11.4.8v2/D8.3.4v2: synchronous pn binders admit direct raw variants.
pn binder_pick(value: number as T, count: int) T {
    var index = 0
    while (index < count) {
        index = index + 1
    }
    return value
}

// The forward raw declaration must be available to a procedural caller.
pn select_int(count: int) int {
    return binder_pick(7, count)
}

let dynamic: any = 9;
[binder_pick(3, 2), binder_pick(2.5, 4), select_int(6), binder_pick(dynamic, 1)]
