// D3.3.3v3, S9.3.1: an admitted array binding can populate a named record
// directly; an unproven array still crosses the field's checked boundary.
type Cell = {value: int}
type Holder = {cells: Cell[], note: string}

let saved: Cell[] = [{value: 3}]

fn from_global() Holder => {cells: saved, note: "global"}
fn from_param(cells: Cell[]) Holder => {cells: cells, note: "param"}
fn from_dynamic(cells: array) Holder => {cells: cells, note: "dynamic"}

pn main() {
    print(from_global().cells[0].value); print(" ")
    print(from_param([{value: 5}]).cells[0].value); print(" ")
    print(from_dynamic([{value: 7}]).cells[0].value); print(" ")
    print(from_dynamic([8]) is error); print("\n")
}
