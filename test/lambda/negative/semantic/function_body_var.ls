// S12.1.4v2(2): `var` is procedural, so a `function` body rejects it.
function counts(f: function, x) { var y = x; f(y) }
pn main() { print(1) }
