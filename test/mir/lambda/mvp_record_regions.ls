// D4.2.5/D4.3.3: contextual construction validates fields before packed reads.
type Row = {flag: bool, value: int}
type Choice = {value: int | string}
fn typed_read(row: Row) int => row.value
fn erased_read(row) any => row.value
fn construct(value: any) Choice^ { return {value: value} }
fn parsed(text: string) Choice^ { return {value: parse(text, "json")} }
pn write(var row: Row, value: int) any { row.value = value; return row.value }
pn main() {
    var row: Row = {flag: true, value: 7}
    print([typed_read(row), erased_read(row), erased_read({value: "s"}), erased_read({})]); print("\n")
    let snapshot = row
    write(row, 9)^
    print([row.value, snapshot.value]); print("\n")
    let admitted_int = construct(7)^
    let text = construct("s")^
    print([admitted_int.value, text.value, construct(true) is error, parsed("true") is error]); print("\n")
}
