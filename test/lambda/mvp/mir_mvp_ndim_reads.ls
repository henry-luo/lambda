// S8.2.4v3/S7.1.1v3: every coordinate participates; misses remain null.
fn read(value: any) any => value[1, 2]
fn allocated(value: int) int {
    let churn = for (i in 1 to 40) [i, float(i) + 0.25]
    value
}
let matrix = reshape([1, 2, 3, 4, 5, 6], [2, 3])
let cube = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
let floating = reshape([1.000000000000001, 2.5, 3.5, 4.5], [2, 2])
let retained = floating[0, 0]
let churn = for (i in 1 to 90) [float(i) + 0.25];
[matrix[0, 2] == 3, matrix[allocated(1), allocated(2)] == 6,
 cube[1, 0, 1] == 6, retained == 1.000000000000001,
 matrix[-1, 0] is null, matrix[0, 1.5] is null,
 matrix[0, 0, 0] is null, read("invalid") is null, read(null) is null,
 matrix[last, last] == 5]
