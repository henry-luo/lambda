// LR03-26 (S11.4.6, S11.1.2v2): an inline pattern island compiled only when it
// was evaluated as a value, and nothing evaluates one inside a type, so as a
// constrained base -- `\(d+) that len(~) > 2` -- it had no regex and admitted
// nothing; so did one in a union arm, a map field or an array element. It now
// compiles where the type resolves. Found with it: the JIT tested a
// `case \(d+):` arm with `==`, so the arm never matched (S1.6), and `is`
// raised an error for a value outside the pattern's domain, where the domain
// check makes it false. Golden written from the ruling, not from the runtime.

type Digits = \(d+) that len(~) > 2;
type D = \(d+);
type Digits2 = D that len(~) > 2;
type U = \(d+) | int;
type M = {a: \(d+)};
type A = \(d+)[];
type Sym = \symbol(w+) that len(~) < 4;
fn arm(v) => match v { case \(d+): "digits" case int: "int" default: "other" };
fn named(v) => match v { case D: "D" default: "other" };
fn either(v) => match v { case \(d+) | int: "digits or int" default: "other" };
"-- an inline island as a constrained base --";
["1234" is Digits, "12" is Digits, "abcd" is Digits, "1234" is Digits2, "12" is Digits2];
['ab' is Sym, 'abcd' is Sym, "ab" is Sym];
"-- in a union arm, a map field and an array element --";
["12" is U, 5 is U, "ab" is U, {a: "12"} is M, {a: "ab"} is M, ["1", "2"] is A, ["1", "x"] is A];
"-- as a match arm, inline and named --";
[arm("12"), arm(5), arm("ab"), named("12"), named("ab"), either("12"), either(5), either("ab")];
"-- a value outside the pattern's domain is no member --";
[5 is D, null is D, 'ab' is D, "12" is D, 5 is \(d+)]
