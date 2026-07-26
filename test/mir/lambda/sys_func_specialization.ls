// MT5 fixture, revived from the orphaned test/lambda/unboxed_sys_func.transpile
// and sys_func_native_math.transpile.
//
// Those fixtures asserted that typed arguments select unboxed system-function
// variants (fn_pow_u, fn_abs_i, ...) and native C math (sin(, sqrt().
//
// Tune6 L3 ported the *native math* half to MIR-Direct: math.* transcendentals
// with statically-numeric scalar arguments now call the libm symbol directly
// through a d->d prototype, so sin_float/sqrt_float below import `sin`/`sqrt`
// rather than `fn_math_sin`/`fn_math_sqrt`. abs_int deliberately still routes
// through boxed fn_abs — abs is type preserving (abs(-5) is int 5) and a
// double-returning native call would change that. The unboxed fn_*_u variants
// remain a C-transpiler-only gap.
//
// This fixture records what MIR-Direct actually emits today; if the remaining
// specializations are ported, it fails and must be updated deliberately, which
// is exactly the notification we want.
// Checked by sys_func_specialization.mir-check.

fn pow_float(x: float, y: float) { x ** y }
fn abs_int(x: int) { abs(x) }
fn sin_float(x: float) { math.sin(x) }
fn sqrt_float(x: float) { math.sqrt(x) }

[pow_float(2.0, 3.0), abs_int(-5), sin_float(0.0), sqrt_float(4.0)]
