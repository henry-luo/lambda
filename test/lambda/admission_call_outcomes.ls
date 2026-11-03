// S7.7.3/S7.5.1v6: parameter admission belongs to the call's outcome.
fn opaque(value) => value
fn checked(value: int) int => value
fn failed_nested() any^ => error("nested")
fn forward() int^ => later(opaque("bad"))
fn later(value: int) int => value
fn local_check() int { let value: int = opaque("bad"); 7 }
pn discard_local_check() { local_check(); 42 }
function dynamic(f: function) any^ => f(error("callback"))
fn observes(value: any) int => if (value is error) 42 else 0
fn rows(columns: any) array^ => if (columns is error) columns else columns
fn columns(a, b, model) => [[a, b, model]]
fn nested(batch) any^ {
    let model = error("model");
    rows(batch(columns(1, 2, model)))
}
let accepted = dynamic(observes)
let rejected = dynamic(checked)
let good = checked(opaque(2))
let bad = checked(opaque("bad"))
let recovered = checked(opaque("bad")) ^ { 9 }
let forwarded = forward()
let piped = opaque(3) |> checked()
let nested_scalar = checked(len(failed_nested()))
let nested_pipe = (failed_nested() |> len()) |> checked();
[good, bad is error, recovered, forwarded is error, piped, local_check(), accepted, rejected.message,
    observes(checked(opaque("bad"))), nested((value: any) any => value).message, nested_scalar is error, nested_pipe is error]
