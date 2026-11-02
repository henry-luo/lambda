// S12.1.3v2/S11.4.3: views own pure-function return admission and acknowledgement.
fn opaque(value) => value
fn failed() element^ => error("view outcome")
view good: <view_good>() int { opaque(7) }
view bad: <view_bad>() int { opaque("wrong") }
view declared_any: <view_open>() any { error("view outcome") }
view ordinary_union: <view_union>() element | error { failed() }
view propagated: <view_propagated>() element^ { failed()^ }
view numeric: <view_numeric>() float { opaque(7) }
view dynamic_template: <view_dynamic> { ~.failure }
let numeric_result = apply(<view_numeric>)
let dynamic_result = apply(<view_dynamic failure:error("dynamic")>)
let good_result = apply(<view_good>)
let bad_result = apply(<view_bad>)
let open_result = apply(<view_open>)
let union_result = apply(<view_union>)
let propagated_result = apply(<view_propagated>);
[good_result, bad_result is error, open_result.message, union_result.message,
 propagated_result.message, type(numeric_result) == float, dynamic_result is error]
