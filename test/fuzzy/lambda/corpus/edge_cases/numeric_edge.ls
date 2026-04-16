// Numeric edge cases
// Tests integer/float boundaries, special values, precision

// Integer boundaries
let int1 = 0
let int2 = -0
let int3 = 2147483647  // max int32
let int4 = -2147483648  // min int32

// Float boundaries
let float1 = 0.0
let float2 = -0.0
let float3 = 1.7976931348623157e+308  // Near max double
let float4 = 2.2250738585072014e-308  // Near min positive double

// Special values
let special1 = inf
let special2 = -inf
let special3 = nan

// Operations with special values
let op1 = inf + 1
let op2 = inf - inf  // nan
let op3 = nan == nan  // false
let op4 = 1 / 0  // inf
let op5 = 0 / 0  // nan
let op6 = -1 / 0  // -inf

// Precision edge cases
let prec1 = 0.1 + 0.2  // Famous floating point issue
let prec2 = 1.0 - 0.9
let prec3 = 10000000000000000.0 + 1.0  // Lost precision

// Integer vs float division
let div1 = 1 / 2
let div2 = 1.0 / 2.0
let div3 = 5 / 2
let div4 = 5 / 2.0

// Very small and very large numbers
let tiny = 1e-300
let huge = 1e300
let op7 = tiny * tiny  // Underflow
let op8 = huge * huge  // Overflow to inf

// Negative zero behavior
let negzero1 = -0
let negzero2 = 0 - 0
let negzero3 = -0.0
let negzero4 = (0 == -0)  // Should be true

[int1, float1, op1, op4, op5, prec1, div1, negzero4]
