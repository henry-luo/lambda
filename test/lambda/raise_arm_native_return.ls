// RV9/RV17/RV18: a `^E` fn whose body is `if (...) { raise ... } else { <T> }`
// types as `T | error` and returns on the NATIVE lane (d for float, i64 for
// int/i64); the raise arms exit on the error lane. Both arms, both arm
// orders, and every consumer form must keep working — a regression here shows
// up as `nan`/garbage (the error arm reaching a lane converter) or as an
// uncontained error aborting the script, rather than as a crash.
fn half(x: float) float^ {
    if (x < 0.0) { raise error("negative") } else { x / 2.0 }
}
fn half_flipped(x: float) float^ {
    if (x >= 0.0) { x / 2.0 } else { raise error("negative") }
}
fn twice(x: int) int^ {
    if (x < 0) { raise error("negative") } else { x * 2 }
}
fn wide(ok: bool) i64^ {
    if (ok) { 123456789012345i64 } else { raise error("wide") }
}

let ok^ok_err = half(8.0)
let bad^bad_err = half(-8.0)
let flip_ok^flip_ok_err = half_flipped(8.0)
let flip_bad^flip_bad_err = half_flipped(-8.0)
let int_ok^int_ok_err = twice(21)
let int_bad^int_bad_err = twice(-21)
let wide_ok^wide_ok_err = wide(true)
let wide_bad^wide_bad_err = wide(false)

"destructure"
[ok, type(ok_err)]
[bad, type(bad_err)]
[flip_ok, type(flip_ok_err)]
[flip_bad, type(flip_bad_err)]
[int_ok, type(int_ok_err)]
[int_bad, type(int_bad_err)]
[wide_ok, type(wide_ok_err)]
[wide_bad, type(wide_bad_err)]
"or-recovery"
// the int error case is the one that regressed historically: the native
// numeric operand path must not consume the error lane before `or` contains it
[half(6.0) or 99.0, half(-6.0) or 99.0]
[twice(3) or 99, twice(-3) or 99]
[wide(true) or 7i64, wide(false) or 7i64]
"arithmetic on a recovered value"
(half(10.0) or 0.0) + 1.0
(twice(-2) or 10) + 1
