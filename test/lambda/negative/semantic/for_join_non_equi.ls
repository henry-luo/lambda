// @expect-error: E239
// @description: FC5/S14.1.3: join `on` is a conjunction of equalities, the only
// shape a hash join executes; a non-equi condition belongs in `where`.

let xs = [1, 2];
let ys = [2, 3];
[for (x in xs, y in ys on x < y) [x, y]]
