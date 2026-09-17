// LR12-12 (S9.3.1): `push` inserts its value like a literal element, so the
// stored element is a snapshot. A later write through the pushed binding --
// a local, a `var` parameter, or a re-borrowing callee -- must not reach it.
// LR12-13 (D3.2.4v3): a nested store that steps from a declared array into an
// element record keeps the array's contract on both tiers.
type Box = {size: int, tag: int}
type Holder = {items: Box[], n: int}

pn local_push() int {
    var x: Box = {size: 1, tag: 0}
    var bag: Box[] = []
    push(bag, x)
    x.size = 5
    return bag[0].size * 100 + x.size
}

pn param_push(var b: Box, var bag: Box[]) int {
    push(bag, b)
    b.size = 5
    return bag[0].size * 100 + b.size
}

// the pushed-into owner is a field place of a var parameter
pn place_push(var h: Holder) int {
    var x: Box = {size: 2, tag: 0}
    push(h.items, x)
    x.size = 7
    return h.items[0].size * 100 + x.size
}

pn untyped_push() int {
    var x = {size: 1}
    var bag = []
    push(bag, x)
    x.size = 4
    return bag[0].size * 100 + x.size
}

pn stash(var b: Box, var bag: Box[]) { push(bag, b) }

// the push happens in a callee that re-borrows both parameters
pn via_var(var b: Box, var bag: Box[]) int {
    stash(b, bag)
    b.size = 9
    return bag[0].size * 100 + b.size
}

// LR12-13: index-then-field stores under a declared record array
pn literal_then_store() int {
    var bag: Box[] = [{size: 3, tag: 0}]
    bag[0].size = 6
    return bag[0].size
}

pn fresh_push() int {
    var bag: Box[] = []
    push(bag, {size: 3, tag: 0})
    bag[0].size = 6
    return bag[0].size
}

pn named_push_then_store() int {
    var bag: Box[] = []
    var x: Box = {size: 3, tag: 0}
    push(bag, x)
    bag[0].size = 6
    return bag[0].size * 100 + x.size
}

// a fresh local whose only use is the push is moved, not captured; any
// other use (a loop re-push, a later read, a closure, a second push) keeps
// the capture
pn moved() int {
    var bag: Box[] = []
    var x: Box = {size: 1, tag: 0}
    push(bag, x)
    bag[0].size = 7
    return bag[0].size
}

pn reused_in_loop() int {
    var bag: Box[] = []
    var x: Box = {size: 1, tag: 0}
    var i = 0
    while (i < 3) {
        push(bag, x)
        i = i + 1
    }
    bag[0].size = 7
    return bag[0].size * 100 + bag[1].size * 10 + bag[2].size
}

pn fresh_in_loop() int {
    var bag: Box[] = []
    var i = 0
    while (i < 3) {
        var x: Box = {size: i, tag: 0}
        push(bag, x)
        i = i + 1
    }
    bag[0].size = 7
    return bag[0].size * 100 + bag[1].size * 10 + bag[2].size
}

pn used_after() int {
    var bag: Box[] = []
    var x: Box = {size: 1, tag: 0}
    push(bag, x)
    bag[0].size = 7
    return bag[0].size * 10 + x.size
}

pn closure_seen() int {
    var bag: Box[] = []
    var x: Box = {size: 1, tag: 0}
    let peek = () => x.size
    push(bag, x)
    bag[0].size = 7
    return bag[0].size * 10 + peek()
}

pn twice() int {
    var bag: Box[] = []
    var x: Box = {size: 1, tag: 0}
    push(bag, x)
    push(bag, x)
    bag[0].size = 7
    return bag[0].size * 10 + bag[1].size
}

pn store_through(var bag: Box[], v: any) {
    bag[0].size = v
}

pn main() {
    print("local=" ++ local_push() ++ "\n")
    var c: Box = {size: 1, tag: 0}
    var bag: Box[] = []
    print("param=" ++ param_push(c, bag) ++ " caller=" ++ c.size ++ "\n")
    var h: Holder = {items: [], n: 0}
    print("place=" ++ place_push(h) ++ " caller=" ++ h.items[0].size ++ "\n")
    print("untyped=" ++ untyped_push() ++ "\n")
    var d: Box = {size: 1, tag: 0}
    var bag2: Box[] = []
    print("via_var=" ++ via_var(d, bag2) ++ " caller=" ++ d.size ++
        " bag=" ++ bag2[0].size ++ "\n")
    print("literal=" ++ literal_then_store() ++ "\n")
    print("fresh=" ++ fresh_push() ++ "\n")
    print("named=" ++ named_push_then_store() ++ "\n")
    var boxes: Box[] = [{size: 3, tag: 0}]
    let rejected = store_through(boxes, "x")
    print("rejected=" ++ (rejected is error) ++ " size=" ++ boxes[0].size ++ "\n")
    let stored = store_through(boxes, 4)
    print("stored=" ++ (stored is error) ++ " size=" ++ boxes[0].size ++ "\n")
    print("moved=" ++ moved() ++ " loop=" ++ reused_in_loop() ++ " fresh=" ++
        fresh_in_loop() ++ " after=" ++ used_after() ++ " closure=" ++
        closure_seen() ++ " twice=" ++ twice() ++ "\n")
}
