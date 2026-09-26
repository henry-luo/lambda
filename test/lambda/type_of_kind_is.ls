// Found with LR03-29 (S2.5.1v2, S2.1.1v4): `type(x)` names a container's or a
// function's kind, and `is` tests the kind. `fn_is` knew `list` and `array` by
// their static wrappers, so the fresh wrapper `type((1, 2))` returns sent the
// bare `list` Type to the validator, which read it as a TypeArray and
// crashed; `type([1, 2])`, `type({})` and `type(f)` returned a bare prefix
// that the same readers took for the extended struct. Each kind is now its
// singleton. The generic kinds are bare Types, and `name()` and `<:` had read
// a shape past their end: `name(map)` and `{a: int} <: map` crashed.
// Golden written from the ruling, not from the runtime.

let f = (x) => x;
let ta = type([1, 2]);
let tl = type((3, 4));
let tm = type({b: 2});
let tf = type(f);
let te = type(<p>);
"-- `is` against the type of a value tests its kind --";
[[1] is ta, (1, 2) is ta, (1 to 3) is ta, [1, 2] is tl, (1, 2) is tl, {a: 1} is tm, [1] is tm];
[f is tf, 5 is tf];
"-- and the kinds print and compare as themselves --";
[ta, tl, tm, tf, te, type(1 to 3)];
[ta == array, tl == list, tl == array, tm == map, tf == function, te == element];
"-- the generic kinds have no shape to read --";
type P = {a: int};
type E = <p>;
type Q = {};
[name(map), name(element), name(object), name(tm), name(type(<p>))];
[object, date, time, list, integer];
[P <: map, map <: P, E <: element, element <: E, map <: Q, object <: map]
