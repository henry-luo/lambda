// RV9: a `^E` fn whose body is `if (...) { raise ... } else { <float> }` types
// as `float | error` and returns on the NATIVE d lane; the raise arms exit on
// the error lane. Both arms, both arm orders, and every consumer form must
// keep working — a regression here shows up as `nan` (the error arm reaching
// it2d) rather than as a crash.
fn half(x: float) float^ {
    if (x < 0.0) { raise error("negative") } else { x / 2.0 }
}
fn half_flipped(x: float) float^ {
    if (x >= 0.0) { x / 2.0 } else { raise error("negative") }
}
// int/i64 raise-arm bodies stay boxed by design: their native lanes have no
// boxed-to-lane conversion at the return boundary.
fn twice(x: int) int^ {
    if (x < 0) { raise error("negative") } else { x * 2 }
}

let ok^ok_err = half(8.0)
let bad^bad_err = half(-8.0)
let flip_ok^flip_ok_err = half_flipped(8.0)
let flip_bad^flip_bad_err = half_flipped(-8.0)
let int_ok^int_ok_err = twice(21)
let int_bad^int_bad_err = twice(-21)

"destructure"
[ok, type(ok_err)]
[bad, type(bad_err)]
[flip_ok, type(flip_ok_err)]
[flip_bad, type(flip_bad_err)]
[int_ok, type(int_ok_err)]
[int_bad, type(int_bad_err)]
"or-recovery"
[half(6.0) or 99.0, half(-6.0) or 99.0]
"arithmetic on a recovered value"
(half(10.0) or 0.0) + 1.0
