// S11.1.7: `none` is the empty type -- it admits no value, `null` and errors
// included, and it is below every type. A type operation reduces to it when
// its literal operands decide that nothing is admitted; `none` is the identity
// of `|` and `!` and absorbs `&`. A reduced form prints, compares and relates
// as its result in value and type context alike.
// Golden written from the ruling, not from the runtime.

let one = 1; let two = 2; let t = int;
"-- the word: a type value that admits nothing --";
[none, type(none), none is type, null is none, 0 is none, "" is none, error("x") is none];
"-- literal-decided emptiness reduces to none --";
[1 & 2, (1 | 2) ! (1 | 2), int & "a", null & int, one & two];
"-- none's identities --";
[t | none, 1 is (none | 1), 2 is (none | 1), int ! none, none ! int, int & none, 3 is (int | none)];
"-- equality: reduced forms compare as their result --";
[(1 & 2) == none, (3 & 4) == (1 & 2), (int | none) == int, none == null, none == number, none == type, number == integer];
"-- relations: below every type, above none only --";
[none <: int, none <: none, none <: (1 | 2), (1 & 2) <: string, int <: none, (1 | 2) <: none];
type E = 1 & 2;
type U = string | none;
type M = {a: none};
"-- type context reduces alike --";
[E, E == none, 5 is E, U, U == string, "a" is U, {a: 1} is M, [] is none[], [1] is none[]];
"-- names --";
[string(none), string(1 & 2), name(none)];
"-- a none return is a contract no value passes: each call is the error --";
fn only_raises(msg: string) none^ { raise error(msg) }
fn passes_through(x) none => x;
let r = passes_through(1);
[r is error, only_raises("boom") or "handled"];
"-- a none arm never matches --";
fn classify(x) => match x { case none: "none" case int: "int" default: "other" };
[classify(5), classify(null), classify("s")]
