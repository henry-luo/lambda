// `or` contains error/null values but deliberately remains conservative about
// ordinary falsy values. The AST metadata test pins the matching type sets.
let parsed = int("not-a-number") or 7
let explicit_any: any = null
let from_any = explicit_any or 7
let absent = null or 7
let false_value = false or 7
let true_value = true or 7

fn maybe_value(value: int) int^ {
  if (value < 0) raise error("negative")
  else value
}

let direct_contained = maybe_value(0 - 1) or 9
let dynamic_maybe = maybe_value
let dynamic_contained = dynamic_maybe(0 - 1) or 11;

[parsed, from_any, absent, false_value, true_value, direct_contained, dynamic_contained]
