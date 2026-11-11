// D4.3.1/D2.3.5: element predictions guard the value; misses retain numeric tags.
fn square(value: any) any => value * value
fn project_square(values: any, key: int) any => square(values[key])
pub fn zero_sign(value: any) any => 1.0 / float(value)
pub pn reduction(values: any, limit: int) any {
    var total = 0.0
    var index = 0
    while (index < limit) {
        total = total + values[index] * values[index]
        index = index + 1
    }
    return total
}
pn main() {
    print([project_square([1.5], 0), project_square([2], 0), project_square([1.5], 2)]); print("\n")
    print([type(project_square(["x"], 0)) == error, type(project_square([2i64], 0)) == i64,
        project_square([2i64], 0) == 4i64]); print("\n")
    print([reduction([1.5, 2.5], 2) == 8.5, reduction([2, 3], 2) == 13.0,
        reduction([1.5], 2) == null, type(reduction(["x"], 1)) == error]); print("\n")
    print([zero_sign(0.0) > 0.0, zero_sign(-0.0) < 0.0,
        type(zero_sign(null)) == error]); print("\n")
}
