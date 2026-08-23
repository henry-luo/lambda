// Native math lowering (Tune6 L3) must be observationally identical to the
// boxed fn_math_* path, including domain errors, NaN/inf and signed zero.
// Scalar args take the native route; vector args must stay element-wise.

// domain errors and infinities
[math.sqrt(-1.0), math.log(0.0), math.log(-1.0), math.asin(2.0), math.acos(2.0)];
[math.sqrt(0.0), math.log(1.0), math.exp(0.0)];
[math.log10(0.0), math.log2(0.0), math.log1p(-1.0)];

// infinities in and out
[math.sqrt(1.0 / 0.0), math.exp(1000.0), math.exp(-1000.0)];

// signed zero preservation
[math.sqrt(0.0), math.cbrt(-0.0)];

// integer arguments widen to double (same as the boxed path's item_to_double)
[math.sqrt(4), math.sqrt(2), abs(-3.5)];
[floor(2.7), ceil(2.1), round(2.5), trunc(-2.7)];

// two-arg family
[math.pow(2.0, 10.0), math.pow(2.0, 0.5), math.pow(-8.0, 1.0 / 3.0)];
[math.pow(0.0, 0.0), math.pow(1.0 / 0.0, 0.0)];
[math.atan2(1.0, 1.0), math.atan2(0.0, -1.0), math.hypot(3.0, 4.0)];
[min(3.5, 2.5), max(3.5, 2.5)];

// trig round trip
[math.sin(0.0), math.cos(0.0), math.tan(0.0)];
[math.atan(1.0) * 4.0];
[math.sinh(0.0), math.cosh(0.0), math.tanh(0.0)];
[math.asinh(0.0), math.acosh(1.0), math.atanh(0.0)];
[math.exp2(10.0), math.expm1(0.0)]

// VECTOR ARGUMENTS must stay element-wise on the boxed path — this is the
// semantic cliff the native lowering must never cross.
let v = [1.0, 4.0, 9.0]
math.sqrt(v)
let w = [0.0, 1.0]
math.exp(w)

// scalar drawn from a vector still goes native
math.sqrt(v[1]);

// nested / chained native calls
[math.sqrt(math.sqrt(16.0))];
[math.pow(math.sqrt(4.0), 3.0)]

// native result feeding integer-typed context
let r = math.sqrt(9.0);
[r, r * 2.0, r == 3.0]
