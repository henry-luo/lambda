// D7.4.1: Core function values carry a checked adapter for a foreign export.
import js: ._mir_mvp_js_exports
import other: ._mir_mvp_js_export_module
fn through(callback: function, first, second) any => callback(first, second)
let captured = js.arity2
let borrowed = other.exported_value()^
let entries = [js.arity0, js.arity1, js.arity8]
let before = through(captured, 2, 3)
let count = js.step()
let after = captured(2, 3)
let zero = entries[0]()
let one = entries[1](7)
let eight = entries[2](1, 2, 3, 4, 5, 6, 7, 8)
let fractional = js.fractional
let scalar_result = fractional(1.0)
let invoke = () => captured(2, 3)
let failed = js.rejected
let failure = failed()
let churn = for (i in 1 to 90) [float(i) + 0.25];
[before == 46, count == 42, after == 47, zero == 42, one == 49, eight == 78,
 scalar_result == 1.000000000000001, invoke() == 47, failure is error, captured == js.arity2,
 borrowed == captured, borrowed(2, 3) == 47]
