// S10.1.1v2: `|`, `&` and `!` are the type operators, and only that. In an
// expression a type operand is itself and a scalar reads as its literal type;
// the result is always a type -- `1 | 1` included -- and `1 & 2` is the empty
// type, which admits nothing (not even null). Set algebra on containers is the
// functions `unique(a, b, ...)`, `intersect(a, b, ...)` and `except(a, b)`.
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
"-- a container has no literal type yet: an operand error --";
([1, 2] | [2, 3]) is error
