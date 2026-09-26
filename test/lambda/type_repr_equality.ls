// LR03-29 (S5.5.2): type equality is representational -- normalized forms
// compare. `==` on two type values compared their payloads' TypeId, which
// every union, intersection, exclusion, occurrence and literal type shares,
// so `(1 | 2) == (3 | 4)` was true. A literal compares by value (S11.2.1), `|`
// and `&` as the sets of their arms, an occurrence by its bounds, a range by
// its bounds, a pattern by its compiled form, a constrained type by its base
// and predicate site (S5.5.1), and the kinds that share one TypeId -- sized
// numbers, `date`/`time`, `list`, `object` -- by what they are. A hash agrees
// with `==` (S5.6.2), so grouping and `unique` see equal types as one.
// LR03-15 and LR03-16, found with LR03-11, are pinned at the end.
// Golden written from the ruling, not from the runtime.

type U1 = (int | string) | bool;
type U2 = int | (bool | string);
type I1 = int & 5;
type I2 = 5 & int;
type E1 = int ! 5;
type Dup = int | int;
type R5 = 1 to 5;
type R5b = 1 to 5;
type R9 = 1 to 9;
type CA = "a" to "e";
type CB = "a" to "e";
type OptI = int?;
type RepI = int{0,1};
type StarI = int*;
type OpenI = int{0+};
type PlusI = int+;
type Arr = int[];
type Arr3 = int[3];
type Arr3b = int[3];
type A = 1;
type B = 1.0;
type S1 = "a";
type S2 = "a";
type Y1 = 'a';
type D1 = \(d+);
type D2 = \(d+);
type W = \(w+);
type Pos = int that ~ > 0;
type Pos2 = Pos;
type Pos3 = int that ~ > 0;
"-- LR03-29: unions and intersections are sets of their arms --";
[(1 | 2) == (3 | 4), (int | string) == (int | bool), (int & 5) == (string ! "a")];
[(1 | 2) == (2 | 1), (int | string) == (string | int), U1 == U2, I1 == I2, Dup == int];
[(1 | 2 | 3) == (3 | 2 | 1), (1 | 2) == (1 | 2 | 3), E1 == E1, E1 == I1];
"-- a reduced operation is its result (S11.1.7) --";
[(3 & 4) == (1 & 2), (int | none) == int, (3 & 4) == none, (1 & 2) == int];
"-- ranges, occurrences and arrays by their bounds --";
[R5 == R5b, R5 == R9, CA == CB, R5 == int, OptI == RepI, StarI == OpenI, StarI == PlusI];
[Arr == Arr, Arr3 == Arr3b, Arr == Arr3];
"-- literals by value --";
[A == B, S1 == S2, S1 == Y1, A == int, (1 | 2) == (1.0 | 2.0)];
"-- patterns by their compiled form, constrained types by their predicate site --";
[D1 == D2, D1 == W, Pos == Pos2, Pos == Pos3, Pos == int];
"-- the kinds that share a TypeId --";
[u8 == u8, u8 == i16, date == date, time == datetime, list == list, list == array];
[object == object, object == map, number == integer, int == int];
"-- the type of a value --";
[type(5) == int, type(5u8) == u8, type(5u8) == i16, type((1, 2)) == list];
[type([1, 2]) == array, type([1, 2]) == list, type({a: 1}) == map, type(<p>) == element];
[type(int) == type, type(1) == type(2), type(1) == type("a")];
"-- a hash agrees with == --";
let ts = [(1 | 2), (2 | 1), int, type(5), (int | none), U1, U2, (1 & 2)];
[for (t in ts group by t into g) len(content(g))];
len(unique(ts));
"-- LR03-15: a sized arm of a union admits its values --";
[5u8 is (u8 | string), 5i16 is (i16 | string), 5u8 is u8, 300 is (u8 | string)];
"-- LR03-16: a literal alias is a type value, not an address --";
type Three = 3;
[3 is Three, 4 is Three, Three <: int, type(Three) == type, Three == 3]
