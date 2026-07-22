// MT5 fixture, revived from the orphaned test/lambda/unboxed_sys_func.transpile
// and sys_func_native_math.transpile.
//
// Those fixtures asserted that typed arguments select unboxed system-function
// variants (fn_pow_u, fn_abs_i, ...) and native C math (sin(, sqrt(). Both
// optimizations exist in the C transpiler only: lambda/transpile-mir.cpp never
// emits an unboxed variant or a native math call, so MIR-Direct routes these
// through the boxed runtime entry points.
//
// This fixture therefore records what MIR-Direct actually emits today. It is a
// documented gap, not an endorsement: if the specializations are ported to
// MIR-Direct, this fixture fails and must be updated deliberately, which is
// exactly the notification we want.
// Checked by sys_func_specialization.mir-check.

fn pow_float(x: float, y: float) { x ** y }
fn abs_int(x: int) { abs(x) }
fn sin_float(x: float) { math.sin(x) }
fn sqrt_float(x: float) { math.sqrt(x) }

[pow_float(2.0, 3.0), abs_int(-5), sin_float(0.0), sqrt_float(4.0)]
