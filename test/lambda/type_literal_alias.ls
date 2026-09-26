// LR03-30: a type alias of one literal is a type value (S11.2.1: a literal
// type is the singleton of its value; S10.1.1v2). Only string and symbol
// literals were wrapped as type values, so `type T = 1` published the literal
// type's address as an int, and a symbol literal's payload was read as a
// String and admitted nothing. `type(x)` of a type value is `type` itself.
// LR03-31: a bool in type position carried no value, so `true` admitted both
// bools, and a bool inside a larger pattern emitted the wrong value.
// Golden written from the ruling, not from the runtime.

type T = 1;
type Z = 0;
type F = 2.5;
type S = "a";
type Y = 'sym';
"-- a one-literal alias is a type value --";
[type(T), type(Z), type(F), type(S), type(Y)];
"-- it admits exactly its value --";
[1 is T, 2 is T, 1.0 is T, 0 is Z, false is Z, null is Z];
[2.5 is F, 2.0 is F, "a" is S, "b" is S, 'a' is S, 'sym' is Y, 'x' is Y, "sym" is Y];
"-- as a contract --";
let one: T = 1;
let sym: Y = 'sym';
fn pick(x: F) => x;
[one, sym, pick(2.5)];
"-- in a match arm --";
fn which(x) => match x { case T: "T" case Y: "Y" case S: "S" default: "other" };
[which(1), which('sym'), which("a"), which(2)];
"-- the type of a type value is type --";
let t = int;
[type(int) == type, type(t) == type, type(T) == type, type(type) == type, T == T];
"-- LR03-31: a bool literal type is its value's singleton --";
type B = true;
type N = false;
[type(B), true is B, false is B, false is N, 1 is B, false is (true | 1), true is (true | 1)];
fn flags(x) => match x { case [true, false]: "tf" case true: "T" case false: "F" default: "other" };
[flags([true, false]), flags([false, false]), flags([false, true]), flags(true), flags(false), flags(0)]
