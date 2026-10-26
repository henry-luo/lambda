// S11.1.1v3: dynamic singleton matrices keep rank through numeric promotion.
fn matrix(row) => [row]
let a = matrix([1.0, 2.0, 3.0]);
[shape(a), shape(transpose(a)), transpose(a)];
transpose(transpose(a)) == a;
let b = [for (i in 0 to 1) [float(i)]];
[shape(b), transpose(b)];
transpose(matrix([inf, inf]));
[reduce(matrix([1, 2]), (a, b) => 999) == [1, 2],
 reduce([[1, 2], [3, 4]], (a, b) => a + b) == [4, 6]];
// S1.6/S5.3.1: packed dimensions count toward the same logical depth limit.
fn deep(n) => if (n == 0) [0] else [deep(n - 1)]
let too_deep = deep(256) == deep(256) ^ { ^ };
[deep(255) == deep(255), too_deep is error]
