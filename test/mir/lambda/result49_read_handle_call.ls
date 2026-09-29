// D4.4.4v4, D5.3.3: a pure direct call preserves a rooted read-only place.
type Cell = {x: int, y: int}
type Box = {cells: Cell[]}

fn observe(box: Box) int => box.cells[0].x
fn observe_key(id: int) int => id + 1
fn observe_alloc(box: Box) int => [box.cells[0].x, 99][0]
fn retain_box(box: Box) Box => box

pn read_across(var box: Box, id: int) int {
    let first: int = box.cells[id].x
    let seen: int = observe(box)
    let second: int = box.cells[id].y
    first + seen + second
}

pn key_across(var box: Box, id: int) int {
    let first: int = box.cells[id].x
    let seen: int = observe_key(id)
    let second: int = box.cells[id].y
    first + seen + second
}

pn read_across_gc(var box: Box, id: int) int {
    let first: int = box.cells[id].x
    let seen: int = observe_alloc(box)
    let second: int = box.cells[id].y
    first + seen + second
}

pn change(var box: Box, id: int) any {
    box.cells[id].x = 7
}

pn read_after_change(var box: Box, id: int) int {
    let first: int = box.cells[id].x
    change(box, id)
    let second: int = box.cells[id].x
    first + second
}

pn read_after_alias(var box: Box, id: int) int {
    let first: int = box.cells[id].x
    let saved: Box = retain_box(box)
    box.cells[id].x = 11
    let old: int = saved.cells[id].x
    let fresh: int = box.cells[id].x
    old * 100 + fresh * 10 + first
}

pn main() {
    var box: Box = {cells: [{x: 2, y: 3}]}
    let snapshot: Box = box
    print(read_across(box, 0)); print(" ")
    print(key_across(box, 0)); print(" ")
    print(read_across_gc(box, 0)); print(" ")
    print(read_after_change(box, 0)); print(" ")
    var alias_box: Box = {cells: [{x: 2, y: 3}]}
    print(read_after_alias(alias_box, 0)); print(" ")
    print(snapshot.cells[0].x); print(" ")
    print(box.cells[0].x); print("\n")
}
