// @expect-error: E239
// @description: S14.1.2v2: row clauses (`let`, `where`) precede `group by`, group
// clauses (`order by`, `limit`, `offset`) follow it, and each clause appears
// once. Each line below once ran its clause in the wrong phase, or let a twin
// replace it, without a diagnostic.

let xs = [1, 2, 3];
[for (x in xs where x > 0 where x < 3) x];
[for (x in xs group by x as k into g where k > 1) k];
[for (x in xs group by x as k into g, let n = len(g)) n];
[for (x in xs order by x group by x as k into g) k];
[for (x in xs limit 1 limit 2) x]
