// Rows of an array literal share one element type only when their own
// elements agree (resolve_array). Before the fix the first row's type stood
// for every row, so a later row's float or string read through an int lane:
// the JIT returned 4 for 4.5 and 0 for "a", and element attributes failed.
let T = [[1, 2], [3, 4.5]];
"float in a later row:";
[for (r in T) r[1]];
"through a map and an element:";
[for (r in T) {y: r[1]}];
[for (r in T) <c y:r[1]>];
"strings after ints:";
let N = [[1, 2], ["a", "b"]];
[for (r in N) r[0]];
"arithmetic on a later float row:";
[for (r in [[1], [2.5]]) r[0] + 1];
"deeper nesting:";
let D = [[[1]], [[1.5]]];
[for (r in D) r[0][0]];
"rows that agree stay typed:";
let I = [[1, 2], [3, 4]];
[for (r in I) r[0] + r[1]]
