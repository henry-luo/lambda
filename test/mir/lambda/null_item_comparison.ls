// S5.1.5 / S7.9.2v3: null equality reads only the Item tag, preserving errors.
fn equals_null(value: any) any => value == null
fn differs_null(value: any) any => null != value
fn indexed(values, i: int) any => values[i] != null
fn local(values, i: int) any {
    let value: any = values[i];
    value == null
}
[for (v in [null, false, true, 0, 1u64, nan, [], {a: 1}, "", type(null), int])
    [equals_null(v), differs_null(v), indexed([v], 0), local([v], 0)]];
[indexed([], 0), local([], 0)];
let failure = error("original");
[equals_null(failure) ^ {"error"}, differs_null(failure) ^ {"error"},
 indexed([failure], 0) ^ {"error"}, local([failure], 0) ^ {"error"}]
