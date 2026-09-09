// Tune22 native int-lane comparisons: compact, null, and poison values.
fn typed_array_cases(values: int[], index: int, limit: int) => [
    values[index] == limit, values[index] != limit,
    values[index] < limit + 1, values[index] >= limit
]
fn nullable_cases(value: int?) => [
    value == null, value != null, value == value,
    value != value, value == 0, value != 0
]
fn int_cases(value: int) => [
    value == value, value != value, value > 0, value < 0
]

let values: int[] = [1, 2]
let absent: int? = values[2]
let saturated = 9007199254740991 * 4
fn divide(a, b) => a div b
let poisoned = divide(0, 0)

{
  compact: typed_array_cases(values, 1, 2),
  absent: nullable_cases(absent),
  saturated: int_cases(saturated),
  poisoned: int_cases(poisoned)
}
