// D5.4.6/D8.3.1: transported place homes survive AUTO publication and loop handoff.
import helper: .place_borrow_homes
type Cell = {k: int}
type Box = {cell: Cell, count: int, values: int[]}
pn increment(var n: int) { n = n + 1 }
pn cycle(var box: Box, rounds: int) int^ {
    var index = 0
    var total = 0
    while (index < rounds) {
        total = total + helper.change(box.cell)
        helper.replace(box.cell)^
        increment(box.count)^
        sleep(1)^
        index = index + 1
    }
    return total
}
pn main() {
    var box: Box = {cell: {k: 12}, count: 0, values: [1, 2]}
    let saved = box
    let total = cycle(box, 40)^
    print([total, box.cell.k, box.count, saved.cell.k, saved.count]); print("\n")
}
