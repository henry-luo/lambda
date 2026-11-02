// S7.4.2v3/S11.4.2v3: equivalent contracts have the same ordinary outcomes.
type Failure = error
fn raised_union() int | error => raise error("raised union")
fn returned_union() int | error => error("returned union")
fn raised_caret() int^ => raise error("raised caret")
fn returned_caret() int^ => error("returned caret")
fn named_union() int | Failure => raise error("named union")
fn invoke_outcome(f: fn () int | error) any^ => f()
fn forward() int | error => raised_union()^
fn returns_any() any => error("any")
let a = raised_union()
let b: any = returned_union()
let c = raised_caret()
let d = returned_caret()
let e = invoke_outcome(returned_union)
let f = forward()
let g = named_union()
let h = call(returned_union, [])
let i = returns_any()
let composed = int("2") + 1
let classified = error("classification") is (error | null);
fn failed_target() any^ => error("target failure")
fn failed_argument() int^ => error("argument failure")
let target_error = failed_target()()
let priority_error = failed_target()(failed_argument())
let method_error = string(error("text target")).lower();
fn default_outcome(value = returned_caret()^) int^ => value
fn default_receiver(value: any = returned_caret()) bool => value is error
let default_error = default_outcome();
[a.message, b.message, c.message, d.message, e.message, f.message,
 g.message, h.message, i.message, composed, classified,
 target_error.message, priority_error.message, method_error.message,
 default_error.message, default_receiver()]
