// Fixture of vibe/impl/Lambda_List_Fixes (done).md, green since P5 (2026-09-23) on
// both tiers. Golden written from the rulings, not from the runtime.
// S11.1.6v2 / S11.1.1v3: the occurrence family (T? T* T+ T{n,m}) is a run —
// the list type; the array family (T[] T[n]) is an array. In a sequence-
// pattern slot an occurrence is a run and zero is void; as a boundary type it
// admits null (0), a bare T (1), a sequence of two or more T (list or array
// image). Annotations never change a value's kind.

"-- occurrence types as boundary types --";
[null is int*, 5 is int*, (5, 6) is int*, [5, 6] is int*, [5] is int*, [] is int*];
[null is int?, 5 is int?, [5] is int?, [] is int?, (5, 6) is int?];
[null is int+, 5 is int+, (5, 6) is int+, [5, 6] is int+, [5] is int+];
[5 is int{1}, null is int{0}, (5, 6) is int{2}, [5, 6] is int{2}, (5, 6, 7) is int{2,3}, (5, 6, 7) is int{2+}, 5 is int{2+}];
[(5, 6) is (int*), "a" is string*, null is string?]
"-- the array family --";
[[] is int[], [5] is int[], (5, 6) is int[], (1, 2) is int[2], [1, 2] is int[2], [1, 2, 3] is int[2], 5 is int[], null is int[]];
[[] is int[0], [5] is int[1], 5 is int[1], null is int[0]]
"-- the families meet at two or more items --";
[(1, 2) is int{2}, (1, 2) is int[2], 5 is int{1}, 5 is int[1], [5] is int{1}, [5] is int[1]]
"-- a field of occurrence type holds the run's image --"
type R = {f: int*};
[{f: null} is R, {f: 5} is R, {f: [5, 6]} is R, {f: [5]} is R, {f: []} is R, {f: (5, 6)} is R, {f: "x"} is R]
let r: R = {f: (5, 6)};
[r is R, type(r.f), r.f]
"-- sequence-pattern slots: a run, void at zero --";
[[1, 2] is [1, int*, 2], [1, 5, 6, 2] is [1, int*, 2], [1, null, 2] is [1, int*, 2], [1, [5, 6], 2] is [1, int*, 2]];
[[1, 2] is [1, int?, 2], [1, 5, 2] is [1, int?, 2], [1, 5, 6, 2] is [1, int?, 2]];
[[1, 2, 3] is [int{3}], [1, 2] is [int{3}], [1, 2, 3] is [int{2+}], [] is [int*], [1, 2] is [int*]]
"-- annotations keep the kind --"
let xs: int* = (1, 2)
let ys: int[] = (1, 2)
let zs: int* = for (x in [3]) x
let ws: int* = for (x in []) x;
[type(xs), type(ys), zs, ws == null];
[xs, 9];
[ys, 9]
fn evens(a: int[]) int* => for (x in a where x % 2 == 0) x;
[evens([1, 3]) == null, evens([2, 3]), type(evens([2, 4]))];
[evens([2, 4]), 9]
"-- string islands use the regex count --";
["123" is \(d{3}), "12" is \(d{3}), "1234" is \(d{2,4}), "12345" is \(d{2,4}), "12" is \(d{2+})]
"-- type relations (an occurrence type in value position is bound first: `int*` there reads as multiplication) --"
type IntRun = int*
type IntOpt = int?
type IntSome = int+
type IntPair = int[2]
type IntArr = int[];
[int <: IntRun, IntOpt <: IntRun, IntPair <: IntRun, IntArr <: IntRun, IntRun <: IntArr, IntSome <: IntRun, list <: array, range <: array, array <: list]
