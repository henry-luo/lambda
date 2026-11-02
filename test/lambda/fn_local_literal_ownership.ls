// S9.1.2/S9.1.7v2: mutable literals belong to each fn activation.
fn changed(value) {
    var record = {code: 0, data: null}
    record.code = value
    record.data = [value]
    return record
}
fn rebound(value) {
    var record = {code: 0}
    record = {code: 1}
    record.code = value
    return record
}
fn nested(value) {
    var record = {child: {code: 0}}
    record.child.code = value
    return record
}
fn fresh_array(value) {
    var values = [0, 0]
    values[0] = value
    return values
}
fn bare() { return {root: null} }
fn from_call(value) {
    var record = bare()
    record.root = value
    return record
}
let p = from_call(10)
let q = from_call(11)
let first = changed(2)
let second = changed(7)
let a = rebound(3)
let b = rebound(9)
let c = nested(4)
let d = nested(8)
let x = fresh_array(5)
let y = fresh_array(6);
[first.code, first.data[0], second.code, a.code, b.code, c.child.code,
 d.child.code, x[0], y[0], p.root, q.root]
