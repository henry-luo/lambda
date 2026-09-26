// S10.1.1v2: `|`, `&` and `!` are the type operators. Two containers meet as
// value sets; a type operand, or two scalars, make a type operation, a scalar
// reading as its literal type. In expression context the operation collapses
// when a literal set decides that it admits one value or none; in type context
// it never collapses. Fixes LR07-37: `e[(int | null)]` is a type query.
// Golden written from the ruling, not from the runtime.

let one = 1; let two = 2; let t = int; let s = "a";
"-- two containers: the value set operation --";
[[1, 2] | [2, 3], [1, 2] & [2, 3], [1, 2] ! [2, 3]];
"-- two scalars are literal types: a type, or its one value --";
[type(1 | 2), 2 is (1 | 2), 3 is (1 | 2), 1 | 1, 1 & 1, 1 & 2];
[(1 | 2) & 2, (1 | 2) ! 1, type((1 | 2) ! 3), 'x' | 'x', type('x' | 'y'), ('x' | 'y') & 'x'];
"-- a type with a value --";
[type(int | null), null is (int | null), int & 5, int & "a", (5 | "a") & int, (5 | "a") ! int];
[type(int ! 5), 5 is (int ! 5), 6 is (int ! 5), type(int & string)];
"-- operands known only at run time --";
[one | one, type(one | two), 2 is (one | two), t & one, type(t | null), s | s, type(s | "b")];
"-- type context never collapses: a type, as `type t = 1` is not the value 1 --";
type T1 = 1 & 2; type T2 = 1 | 1;
[type(T1), 1 is T1, null is T1, type(T2), 1 is T2];
"-- LR07-37: an inline null-admitting key is a type query --";
[count([1, null, 2][(int | null)]), count([1, null, 2]?(int | null)), count(<e 1 null 2>[(int | null)])];
"-- a container beside a type is neither a type nor a scalar --";
(int | [1, 2]) is error
