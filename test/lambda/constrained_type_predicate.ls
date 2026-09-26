// S11.4.6, S10.1.7v2, S12.1.1v2 (LR03-24): a `that` predicate is an ordinary
// `fn` expression over its declaring scope, evaluated in full on every tier.
// T0 had evaluated only an allow-list of literals, operators and pure system
// functions and answered `false` for anything else, where the JIT evaluated
// the body, so `auto` flipped a hot function's answer on promotion.
import .mod_constrained_types;
let lim = 3;
fn dbl(x) => x * 2;

"1 bindings, calls, containers, pipes and comprehensions";
type Big = int that ~ > lim;
type Dbl = int that dbl(~) > 6;
type InSet = int that ~ in [1, 2, 3];
type AllPos = int[] that len(~ |: ~ <= 0) == 0;
type SumBig = int[] that sum(for (v in ~) v * 2) > 10;
[5 is Big, 2 is Big, 5 is Dbl, 3 is Dbl, 2 is InSet, 5 is InSet,
 [1, 2] is AllPos, [1, -2] is AllPos, [3, 4] is SumBig, [1, 2] is SumBig];

"2 let groups, lambdas, match and nested constrained tests";
type LetIn = int that (let d = ~ * 2, d > 6);
type Lam = int that ((x) => x > lim)(~);
type Mat = any that (match ~ { case int: ~ > lim
  default: false });
type Both = int that ~ is Big and ~ < 10;
[5 is LetIn, 2 is LetIn, 5 is Lam, 2 is Lam, 5 is Mat, "a" is Mat,
 5 is Both, 12 is Both];

"3 alias chains and match arms";
type Small = Big that ~ < 10;
fn kind(x) => match x {
  case Small: "small"
  case int that ~ > lim * 10: "huge"
  default: "other"
};
[5 is Small, 50 is Small, kind(5), kind(50), kind(1)];

"4 local types, read in place and from a nested closure";
fn local(n) {
  let bound = n + 1;
  type T = int that ~ > bound;
  type U = int that (let d = ~ - bound, d > 0);
  type S = int[] that sum(for (v in ~) v - bound) > 0;
  let chk = (x, y) => {
    let a = 11;
    let b = 22;
    [x is T, x is U, y is S, a, b]
  };
  [5 is T, 1 is T, chk(5, [5, 5]), chk(1, [1, 1])]
};
local(3);

"5 a predicate re-entered through a call keeps its own names";
type Down = int[] that all([for (v in ~) (inner(v) and v == ~[0])]);
fn inner(n) => if (n > 0) [n - 1] is Down else true;
[[2] is Down, [3] is Down, [0] is Down, [3, 1] is Down];

"6 a predicate that answers an error value fails the check";
fn checked(x) int^ { if (x < 0) raise error("negative") else x }
type Checked = int that checked(~) > 3;
[5 is Checked, -5 is Checked];

"7 a procedure reaching a predicate as a value fails its colour check";
pn eff(x) { print("effect "); x }
fn pure(x) => x;
let pick = (flag) => if (flag) eff else pure;
let via_pn = pick(true);
let via_fn = pick(false);
type ViaFn = int that via_fn(~) > 3;
type ViaPn = int that via_pn(~) > 3;
[5 is ViaFn, 5 is ViaPn];

"8 an imported predicate reads its own module's constants";
let first = "bob";
["admin" is Named, "bob" is Named, first is Named, "bobby" is Short];

"9 a hot function keeps its answer when it is compiled";
fn check(x) => x is Big;
[for (i in 1 to 8) check(5)]
