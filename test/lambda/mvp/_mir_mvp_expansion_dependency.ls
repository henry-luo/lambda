pub let dependency_rows = [["a", "bb"], ["ccc"]]

fn make_seed() int[]^ => fill(2, 3)
let module_seed: int[] = make_seed()^

pub fn first_seed() int^ => module_seed[0]
pub fn default_external(value: int, other: int = value + 2) int => other
pub fn imported_numbers(count: int) int[]^ =>
    if (count < 0) { raise error("imported") } else fill(count, 5)
pub fn imported_tiny() float^ => 1e-300

pub pn replace_first(var values: int[], value: int) int^ {
    values[0] = value
    return len(values)
}
