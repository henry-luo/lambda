// LR03-21 (S11.1.4v2): `A <: B` holds exactly when every value `A` admits is
// admitted by `B`, so a union's arms may split a range between them. `<:`
// tried each arm whole, and `1 to 2 <: 1 | 2` was false. Each arm now covers
// the part of the range it admits -- a same-domain range its overlap, a
// literal its one member, a type holding the whole domain all of it. The
// S11.1.7 reduction splits a range the same way, from range and literal arms.
// Golden written from the ruling, not from the runtime.

type R = 1 to 2;
type OneTwo = 1 | 2;
type R5 = 1 to 5;
type Split = (1 to 3) | (4 to 5);
type Split2 = (1 to 3) | 4 | 5;
type Gap = (1 to 2) | (4 to 5);
type Over = (1 to 4) | (3 to 5);
type Wide = (0 to 2) | (2 to 9);
type Mixed = (1 to 3) | int;
type Floats = 1.0 | 2.0;
type C = "a" to "c";
type CSplit = "a" | "b" | "c";
type CGap = "a" | "c";
type CR = ("a" to "b") | "c";
"-- a union's arms may split a range --";
[R <: OneTwo, R5 <: Split, R5 <: Split2, R5 <: Over, R5 <: Wide, R5 <: Mixed, R <: Floats];
[C <: CSplit, C <: CR];
"-- a member no arm admits leaves the range out --";
[R5 <: Gap, C <: CGap, R5 <: OneTwo, R5 <: (1 | 2 | 3 | 4)];
"-- the other direction is unchanged --";
[OneTwo <: R, Split <: R5, Gap <: R5, CSplit <: C];
"-- the S11.1.7 reduction splits a range the same way --";
[(1 to 3) ! (1 | 2 | 3), (1 to 3) ! (1 | 3), (1 to 3) & (1 | 2 | 3), (1 to 4) ! ((1 to 2) | (3 to 4))]
