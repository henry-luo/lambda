// Test: Error Type Basic Operations
// Layer: 3 | Category: datatype | Covers: error creation, type, handling

error("test error")
type(error("test"))
error("test") is error
if (error("x")) 1 else 0
error("x") or 42
1 + "string"
5 % 0
fn may_fail(x) int^ {
    if (x == 0) raise error("zero")
    else x * 2
}
may_fail(5)^
let val^err = may_fail(0)
val
^err
