// An array literal with spread items holds the spread operands' elements, so
// its element type is theirs, not the operand containers' (resolve_array).
// Before the fix, `{s: c[0]}` stored null: c[0] was typed as an array.
let c = [*[2, 7], *[5, 9]];
"ints from two spreads:";
[for (i in 0 to len(c) - 1) {s: c[i]}];
"a spread and a literal:";
let d = [*[1, 2], 3];
{first: d[0], last: d[2], sum: d[0] + d[2]};
"floats:";
let f = [*[1.5], *[2.5]];
{x: f[1] * 2.0};
"strings:";
let s = [*["a", "b"], "c"];
{joined: s[0] ++ s[2]};
"mixed element types stay generic:";
let m = [*[1, 2], *["x"]];
{a: m[0], b: m[2]};
"spread of field comprehensions:";
let marks = [{s: 2, e: 5}, {s: 7, e: 9}];
let cuts = sort(unique([*[for (k in marks) k.s], *[for (k in marks) k.e]]));
[for (i in 0 to len(cuts) - 2) {s: cuts[i], e: cuts[i + 1]}]
