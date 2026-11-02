// S7.7.2v2/S2.5.3v2: local validation does not manufacture a block result.
type Record = { count: int }
fn opaque(value) => value
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
[record is error, capture(), recovered, declaration_only, rows[0] is error, rows[1].message,
 node[0] is error, node[0].message]
