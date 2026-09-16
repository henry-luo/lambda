// D8.3.4v3 (O11v2): a `var` position whose value travels boxed on both edges is
// admitted to a raw variant. The borrow's write-back is the boxed path's own
// CW33 home transport (D8.1.1v10), so the caller must observe every mutation
// the callee makes, on every tier, exactly as it does through `_b`.

type Counter = {hits: int, tag: int}

pn bump(var c: Counter, step: number as T) T {
    c.hits = c.hits + 1
    c.tag = c.tag + 1
    return step
}

pn drive() Counter {
    var c: Counter = {hits: 0, tag: 100}
    let a = bump(c, 3)
    let b = bump(c, 4)
    return c
}

pn main() {
    let done = drive()
    print(done.hits)
    print(done.tag)
    var solo: Counter = {hits: 5, tag: 5}
    let kept = bump(solo, 2.5)
    print(solo.hits)
    print(kept)
}
