// S9.1.3v3/D5.4.6: packed scalar places use boxed homes and retain exact lanes.
import helper: .place_borrow_homes
type Scalars = {tiny: float, flag: bool, text: string, wide: i64}
type Cell = {k: int}
type OptionalBox = {cell: Cell?}
type RequiredBox = {cell: Cell, count: int, values: int[]}
pn tiny(var x: float) { x = 1.0e-300 }
pn flag(var x: bool) { x = true }
pn text(var x: string) { x = "changed" }
pn wide(var x: i64) { x = 9223372036854775806i64 }
pn optional(var x: Cell?) int {
    let saved = x
    x = {k: 50}
    return saved.k
}
pn initialize(var x: Cell?) { x = {k: 60} }
pn main() {
    var scalars: Scalars = {tiny: 1.0, flag: false, text: "old", wide: 1i64}
    let saved = scalars
    tiny(scalars.tiny)^
    flag(scalars.flag)^
    text(scalars.text)^
    wide(scalars.wide)^
    print([scalars.tiny == 1.0e-300, scalars.flag, scalars.text, scalars.wide]); print("\n")
    print([saved.tiny, saved.flag, saved.text, saved.wide]); print("\n")
    var box: OptionalBox = {cell: {k: 4}}
    print([optional(box.cell), box.cell.k]); print("\n")
    var imported: RequiredBox = {cell: {k: 2}, count: 1, values: [3, 4]}
    print([helper.change(imported.cell), imported.cell.k]); print("\n")
    helper.replace(imported.cell)^
    print(imported.cell.k); print("\n")
    let outcome = helper.returned_snapshot(imported.cell)
    print([outcome.saved.k, outcome.data, imported.cell.k]); print("\n")
    box.cell = null
    initialize(box.cell)^
    print(box.cell.k); print("\n")
}
