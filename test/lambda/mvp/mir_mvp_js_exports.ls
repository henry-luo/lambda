// D7.4.1/D6.2.3: foreign entries retain their owner and caller scalar homes.
import js: ._mir_mvp_js_exports
let values = [js.arity0(), js.arity1(1), js.arity2(1, 2), js.arity3(1, 2, 3),
    js.arity4(1, 2, 3, 4), js.arity5(1, 2, 3, 4, 5), js.arity6(1, 2, 3, 4, 5, 6),
    js.arity7(1, 2, 3, 4, 5, 6, 7), js.arity8(1, 2, 3, 4, 5, 6, 7, 8)]
let scalar_result = js.fractional(1.0)
let text = js.describe("lambda")
let failure = js.rejected()
let first = js.step()
let second = js.step()
let after = js.arity2(2, 3)
let churn = for (i in 1 to 90) [float(i) + 0.25];
[values == [41, 42, 44, 47, 51, 56, 62, 69, 77], scalar_result == 1.000000000000001,
 text == "js:lambda:41", failure is error, first == 42, second == 43, after == 48]
