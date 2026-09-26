// S10.1.1v2: `|`, `&` and `!` are the type operators, and only that. In an
// expression a type operand is itself and any other value reads as its literal
// type -- a container as the pattern its literal spells in type position -- so
// the result is always a type, the same one type syntax names: `1 | 1` is a
// type, and `1 & 2` is the empty type `none` (S11.1.7). Set algebra on
// containers is the functions `unique(a, b, ...)`, `intersect(a, b, ...)` and
// `except(a, b)`, any number of operands.
// Fixes LR07-37: `e[(int | null)]` is a type query.
// Golden written from the ruling, not from the runtime.

let one = 1; let two = 2; let t = int;
"-- scalars are literal types; the result is a type, never collapsed --";
[type(1 | 2), 2 is (1 | 2), 3 is (1 | 2), type(1 | 1), 1 is (1 | 1), type('x' | 'y')];
"-- the empty type admits nothing, null included --";
[type(1 & 2), 1 is (1 & 2), null is (1 & 2)];
"-- a type with a value --";
[type(int | null), null is (int | null), 5 is (int & 5), 6 is (int & 5), 5 is (int ! 5), 6 is (int ! 5)];
"-- operands known only at run time --";
[type(one | two), 2 is (one | two), type(t | null), null is (t | null), 3 is (t ! 3)];
"-- the value set functions --";
[unique([1, 2], [2, 3]), unique((1, 2), [2, 3], [3, 4]), unique([1], 2), unique("ab", "bc")];
[intersect([1, 2, 3], [2, 3, 4]), intersect([1, 2, 3], [2, 3], [3]), intersect("abc", "bcd")];
[except([1, 2, 3], [2]), except("abc", "b"), except((1, 2, 2, 3), [3])];
"-- LR07-37: an inline null-admitting key is a type query --";
[count([1, null, 2][(int | null)]), count([1, null, 2]?(int | null)), count(<e 1 null 2>[(int | null)])];
"-- every scalar kind is a literal type: bool, datetime, binary --";
let yes = true; let day = t'2025-01-01'; let bytes = b'\xDEAD';
let yb = yes | 1; let db = day | 1; let bb = bytes | 0;
[false is yb, true is yb, day is db, t'2025-01-02' is db, bytes is bb, b'\xBEEF' is bb];
"-- a type arm admits as it does in type syntax --";
let dt = int | datetime; let dd = date | 1; let ff = float | string;
[day is dt, "s" is dt, day is dd, t'10:30' is dd, 5 is ff];
type Short = i16 | string;
[5i16 is Short, "a" is Short, 7i32 is (i32 | string), 5i16 is (i32 | string)];
"-- a container reads as the pattern its literal spells --";
let pair = [1, 2] | 3;
let rec = {a: 1, b: "x"} | null;
let para = <p class: "c", "hi"> | 1;
let span = (1 to 3) | 9;
[[1, 2] is pair, [2, 1] is pair, [1, 2, 3] is pair, 3 is pair, (1, 2) is pair];
[{a: 1, b: "x"} is rec, {a: 2, b: "x"} is rec, {a: 1, b: "x", c: 0} is rec, null is rec];
[<p class: "c", "hi"> is para, <p class: "d", "hi"> is para, <q class: "c", "hi"> is para];
[2 is span, 4 is span, 9 is span, ([int, 1] | 0) == ([int, 1] | 0), [5, 1] is ([int, 1] | 0)];
"-- the set functions take any number of operands --";
[unique([1], [2], [3], [4], [5], [1, 6]), intersect([1, 2, 3], [1, 2, 3], [1, 2], [1, 2], [1, 9])]
