// S11.1.3: either known bound fixes the domain of a successful range.
fn next_integer(value: int) => value + 1
fn characters(value: string) => value ++ "!"
fn from_zero(upper) => [for (value in 0 to upper) next_integer(value)]
fn to_three(lower) => [for (value in lower to 3) next_integer(value)]
fn from_a(upper) => [for (value in "a" to upper) characters(value)]
fn to_c(lower) => [for (value in lower to "c") characters(value)]
fn aliased(upper) {
    let values = 0 to upper;
    [for (value in values) value < 2]
}
fn around(center) => [for (value in (center - 1) to (center + 1)
    where value > 0) next_integer(value)]
from_zero(2)
to_three(1)
from_a("c")
to_c("a")
aliased(2)
around(2)
