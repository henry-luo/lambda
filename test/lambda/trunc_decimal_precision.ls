// S7.1.1v3 and S7.10.5v3: truncation preserves scalar numeric values and vector shape.
let a = decimal("9999999999999999.9")^;
let b = decimal("-9080577866422295.294054228882540025")^;
[string(trunc(a)), type(trunc(a)), string(trunc(b)), type(trunc(b))];
[string(trunc(decimal("0.999999999999999999")^)), string(trunc(decimal("-0.999999999999999999")^))];
[trunc(42), trunc(-3.75), trunc(null)];
trunc([1.9, -1.9, 0.2]);
fn dynamic(x) => trunc(x);
[string(dynamic(a)), type(dynamic(a)), dynamic(-3.75)]
