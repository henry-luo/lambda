// S11.4.6v4/D4.2.5: an error arm preserves normal conversion and reification.
type Row = {count: int, label: string}
type Failure = error
fn opaque(value: any) any => value;
fn generic(T: type, value: any) T^ => value;
fn receive(value: float^) type => type(value);
fn mixed_block(flag: bool) float^ { if (flag) { 7 } else { 7.5 } }
fn mixed_plain(flag: bool) float => if (flag) 7 else 7.5;
fn selected(first: float as T, value: T^) T^ => value;
fn record(value: any) Row^ => value;
fn make_record(count: int) Row^ => {count: count, label: "built"};
fn recover_record(value: any) Row^ {
    let child = make_record(value)
    if (child is error) make_record(42) else make_record(child.count)
}
fn flow_number(value: any) bool {
    if (value is float^) {
        let converted: float^ = value;
        type(converted) == float
    } else false
}
fn flow_record(value: any) bool^ {
    if (value is Row^) {
        let converted: Row^ = value
        let row = converted^;
        row.count == 3 and row.label == "flow" and row.extra == true
    } else false
}
pn rebind() {
    var value: float^ = 7
    let first = value
    value = opaque(9)
    let second = value
    value = opaque(error("rebound"))
    let failure = value
    value = opaque(11)
    ;[type(first) == float, type(second) == float,
      failure.message == "rebound", type(value) == float]
}
pn main() {
    let plain: float = opaque(7)
    let caret: float^ = opaque(7)
    let union: float | error = opaque(7)
    let specific: float | Failure = opaque(7)
    let optional: (float^)? = opaque(7)
    let scattered: float | error | null = opaque(7)
    let absent: (float^)? = opaque(null)
    let kept: float^ = opaque(error("kept"))
    let narrowed: int^ = opaque(7.0)
    let refused: int^ = opaque(7.5)
    let row: Row^ = opaque({label: "row", count: 3, extra: true})
    let rows: Row[]^ = opaque([{label: "one", count: 1}, {label: "two", count: 2}])
    let empty: int[]^ = opaque([])
    let alternatives: (int | float)^ = opaque(7)
    let dispersed: int | error | float = opaque(7.5)
    let callback = generic;
    [type(plain) == float, type(caret) == float, type(union) == float,
     type(specific) == float, type(optional) == float, type(scattered) == float,
     absent == null, kept.message == "kept", type(narrowed) == int,
     refused is error, row.count == 3, row.label == "row", row.extra == true,
     rows[0].count == 1, rows[1].label == "two", len(empty) == 0,
     type(alternatives) == int, receive(7) == float,
     type(mixed_block(true)) == float, type(mixed_block(false)) == float,
     type(mixed_plain(true)) == float, type(dispersed) == float, type(generic(float, 7)) == float, type(callback(float, 7)) == float,
     type(generic(int, 7.0)) == int, generic(int, 7.5) is error,
     type(selected(1, opaque(7))) == float,
     record({label: "return", count: 4}).count == 4,
     flow_number(7), flow_record({label: "flow", count: 3, extra: true}),
     recover_record(opaque("bad")).count == 42, recover_record(7).count == 7,
     make_record(opaque(error("argument"))).message == "argument", rebind()]
}
