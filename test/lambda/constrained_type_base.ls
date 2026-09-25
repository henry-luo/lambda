// S11.2.1, S11.3.1v2: a constrained type admits its base exactly as `x is
// <base>` does, then owes its `that` predicates. The base had been compared by
// TypeId, so a float, union, nullable, array, `any` or `number` base refused
// every value, and a map, element or nominal base took any container of its
// kind. The predicates in sections 1-4 are `true`, so each answer is the
// base's own; the plain test is shown beside it.

"1 scalar bases";
type F = float that true;
type Y = any that true;
type Nm = number that true;
[5 is F, 5 is float, 5 is Y, 5 is Nm, "s" is F]

"2 union and nullable bases";
type U = int | string that true;
type N = int? that true;
[5 is U, "a" is U, true is U, null is N, 5 is N, "a" is N]

"3 array base";
type A = int[] that true;
[[1, 2] is A, ["a"] is A, 5 is A]

"4 map, element and nominal bases";
type M = {a: int} that true;
type E = <div> that true;
type Point { x: int, y: int };
type P2 = Point that true;
let pt = <Point x: 1, y: 2>;
[{a: 5} is M, {a: "x"} is M, {b: 1} is M, <div> is E, <span> is E,
 pt is P2, {x: 1, y: 2} is P2]

"5 inline match arms test their base the same way";
fn arm(x) => match x {
  case int | string that true: "scalar"
  case {a: int} that true: "shaped"
  default: "other"
};
[arm(5), arm("a"), arm({a: 1}), arm({a: "x"})]

// A match arm or alias that names a constrained type runs its predicates, as
// `x is T` does; a `case Pos:` arm had admitted the base alone. An alias chain
// owes every predicate on it, innermost first.
"6 named arms, aliases and chains";
type Pos = int that ~ > 0;
type Small = Pos that ~ < 10;
type Pos2 = Pos;
type Never = int | string that false;
fn named(x) => match x {
  case Never: "never"
  case Small: "small"
  case Pos2: "pos"
  default: "other"
};
[named(5), named(50), named(-5), named("a"), named(true)];
[5 is Small, 50 is Small, -5 is Small, -5 is Pos2]

// A first-class type value carries no predicate in reach, so the generic `is`
// admits the base alone (S11.4.6) -- but the whole base: a union base had
// admitted every value.
"7 first-class type values";
let tu = Never;
[true is tu, 5 is tu, [1] is tu]

// The validator enforces a constrained element's base (S11.4.6); a wrapped
// constrained type reached a TypeId compare and refused every element. Its
// predicate is not evaluated there yet (SO9), so [-1] passes.
"8 constrained elements";
[[1, 2] is Pos[], [] is Pos[], [-1] is Pos[], ["a"] is Pos[],
 {v: [1, 2]} is {v: Pos[]}]
