// S11.4.11 (LR03-28): a `that` predicate is an `fn` body, so it may name its
// own type, and each level is a call of the type's predicate function. Both
// tiers had expanded the body where `is` named the type and crashed compiling
// a body that names itself.

"1 a body naming its own type";
type Rec = int that (~ <= 0 or (~ - 1) is Rec);
[3 is Rec, -1 is Rec, "a" is Rec, 2000 is Rec];

"2 through an alias chain and a match arm";
type Small = Rec that ~ < 10;
fn kind(x) => match x {
  case Small: "small"
  default: "other"
};
[4 is Small, 40 is Small, kind(4), kind(40)];

"3 a local type reading an outer local, from a nested closure";
fn local(n) {
  let floor = n;
  type Down = int that (~ <= floor or (~ - 1) is Down);
  let chk = (x) => x is Down;
  [5 is Down, chk(7), floor]
};
local(2);

"4 recursion over the value's structure";
type Nested = any that (~ is int or (~ is array and len(~ |: ~ is Nested) == len(~)));
[[1, [2, [3]]] is Nested, [1, ["x"]] is Nested, 7 is Nested];

"5 a level that answers an error fails the whole test";
fn checked(x) int^ { if (x == 2) raise error("two") else x }
type Guarded = int that (checked(~) > 0 and (~ <= 1 or (~ - 1) is Guarded));
[1 is Guarded, 3 is Guarded, 5 is Guarded];

"6 the candidate starts a fresh context: `~~` is null inside the body";
type Orphan = any that ~~ == null;
[1, 2] |> (~ is Orphan)
