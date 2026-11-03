// S7.7.2v2/S2.5.3v3: local validation does not manufacture a block result.
type Record = { count: int }
fn admitted_record(value: int) Record {
    let record: Record = {count: value}
    record
}
fn narrowed_record(value: float) Record^ {
    let record: Record = {count: value}
    record
}
fn opaque(value) => value
fn admitted_array() int { let values: int[] = [1, 2]; len(values) }
fn numeric_text(value: int) string { let text: string = "n=" ++ value; text }
fn numeric_suffix(value: float) string { let text: string = value ++ "n"; text }
fn defaulted(value: int?) int {
    let resolved: int = value or^ { 9 }
    resolved
}
fn capture() => {
    let value: int = opaque("bad")
    let read = () => value is error;
    [read(), value is error]
}
let record: Record = opaque({count: "bad"})
let failure: int = opaque("bad")
let recovered = failure ^ { 42 }
let declaration_only = { let local: int = opaque("bad") };
// S7.8.2: collected body outcomes are retained without an outer error union.
let rows = [for (i in 1 to 2) error("row")];
let node = <result error("child")>;
// S10.6.1: concatenation does not retain the left literal's singleton type.
let text: "ab" = "a" ++ "b"
let joined_symbol: 'ab' = 'a' ++ 'b';
[record is error, capture(), recovered, declaration_only, rows[0] is error, rows[1].message,
 node[0] is error, node[0].message, text, joined_symbol, admitted_array(), defaulted(null), defaulted(3),
 admitted_record(11).count, narrowed_record(2.0).count, narrowed_record(0.5) is error,
 numeric_text(4), numeric_suffix(2.5)]
