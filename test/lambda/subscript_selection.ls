// S8.2.4v3: two subscript worlds. A type key answers as XPath does -- the run
// `T*`, a list stepped set-at-a-time, a null value never matching -- while a
// positional selection (a range, a mask, an index array) answers as NumPy
// does: always an array, an index array keeping its own length with null for a
// position out of range. Also pins LR07-32..36, the subscript defects found
// alongside. Golden written from the rulings, not from the runtime.

type table = <table>;
type tr = <tr>;
type td = <td>;
type div = <div>;
type N = int | null;
let a = [10, 20, 30, 40, 50];
let m = [[1, 2, 3], [4, 5, 6]];
let t = reshape([for (i in 0 to 47) i], [2, 4, 6]);

"-- a list is stepped set-at-a-time: every cell of every table --";
let two = <html <table <tr <td "a"> <td "b">>> <table <tr <td "c">> <tr <td "d">>>>;
[two[table][tr][td], count(two[table][tr][td])];
"-- a scalar item has no content; an array is a container of its items --";
[(1, "a", 2)[int] == null, [1, "a", 2][int]];
"-- ? over a list searches each item's descendants, not the items --";
let nest = <r <div <div "x">>>;
[count(nest?<div>), count(nest?<div>?<div>), count(nest?<div>.?<div>)];
"-- ? is [T] applied recursively, so it walks a range's values too --";
[count((1 to 3)?int), count([1 to 3]?int), count((1 to 3)[int])];
"-- a null value never matches, even when T admits null --";
[count([1, null, 2][N]), count([1, null, 2]?N), [*[1, null, 2]?N]];

"-- an index array keeps its length; a bad position reads null --";
[a[[1, 3, 9]], a[[1, -1]], a[[1.0, 2.5]], a[["x", 1]], a[[]]];
"-- over any sequence, and nested index arrays keep their shape --";
[["p", "q", "r"][[2, 0]], (0 to 9)[[2, 5]], <ul <li "a"> <li "b"> <li "c">>[[0, 2]]];
[a[[[0, 1], [2, 3]]], m[[1, 0]], a[(0, 4)]];
"-- a bool array is a mask, on a generic array too --";
[a[[false, true, false, true, false]], ["p", "q", "r"][[true, false, true]]];

"-- LR07-32: a key typed only at run time is dispatched on its value --";
let r = 1 to 3;
let idx = [1, 3];
fn at(x) => a[x];
[a[r], at(1 to 3), at(idx), at("k"), at(1.5), at(-1), at(2)];
"-- LR07-33: a selection is not typed as the element --";
fn tail2(x: int[]) => x[1 to 2];
[{k: a[r]}, {v: m[1, 2]}, tail2([7, 8, 9])];
"-- LR07-34: an N-D key that is not a native int --";
let j = 1.0;
fn row1(i) => m[1, i];
[m[1, j], m[1, 1.5], m[0 to 1, 1], row1(2), "abc"[0, 1]];
"-- LR07-35: fewer keys than axes select a leading-axis view --";
[t[1, 3], t[1, 3] == t[1][3], t[1, 3, 5], t[1, 3, 5, 0]];
"-- LR07-36: last belongs to the innermost subscript's container --";
[a[1 to 3][last], a[2 to 4][last - 1], (a[1 to 3])[last], m[0 to 1][last]]
