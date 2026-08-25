// LR02-9: `&` (intersection) and `!` (exclusion) are type-set operators
// everywhere `|` is. They evaluated correctly as patterns but were rejected in
// annotation position, because the static boundary checker recognised only
// OPERATOR_UNION and the type parser lowered a type-level `&` to OPERATOR_OR.
type U = int | string
type I = number & int
type X = int ! string

// pattern position — unchanged behaviour, kept as the control
let x = 1
let as_pattern = [x is U, x is I, x is X,
    x is (int | string), x is (number & int), x is (int & string), x is (int ! string)]

// annotation position — the half that was broken
let via_alias_union: U = 1
let via_alias_intersect: I = 1
let via_alias_exclude: X = 1
let inline_union: int | string = 1
let inline_intersect: number & int = 1
let inline_exclude: int ! string = 1

// parameter position
fn takes_union(a: int | string) => a
fn takes_intersect(a: number & int) => a
fn takes_exclude(a: int ! string) => a

let out = {
    as_pattern: as_pattern,
    annotations: [via_alias_union, via_alias_intersect, via_alias_exclude,
                  inline_union, inline_intersect, inline_exclude],
    params: [takes_union(1), takes_intersect(1), takes_exclude(1)]
}
out
