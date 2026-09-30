// D3.3.3v3/S7.1.1v3: an admitted record-array element keeps its field layout;
// an invalid index still makes the following member read null.
type Entry = {value: int, flag: bool, text: string}
type Position = {index: int}

fn read_value(entries: Entry[], index: int) any => entries[index].value
fn read_flag(entries: Entry[], index: int) any => entries[index].flag
fn read_text(entries: Entry[], index: int) any => entries[index].text
fn read_nested(entries: Entry[], position: Position) any =>
    entries[position.index].value

pn main() {
    let entries: Entry[] = [{value: 7, flag: true, text: "ok"}]
    print([read_value(entries, 0), read_value(entries, 2),
           read_flag(entries, 0), read_flag(entries, 2),
           read_text(entries, 0), read_text(entries, 2),
           read_nested(entries, {index: 0}),
           read_nested(entries, {index: 2})])
    print("\n")
}
