// P6: Single-expression function inlining
// Functions whose body is a single return statement with a native-eligible
// expression are inlined at call sites, eliminating ABI call overhead.

function square(x) { return x * x; }
function cube(x) { return x * x * x; }
function clamp(x, lo, hi) { return x < lo ? lo : x > hi ? hi : x; }
function fma(a, b, c) { return a * b + c; }
function double_val(x) { return x * 2; }
function is_positive(n) { return n > 0; }

// Basic inline calls
var s5 = square(5);           // 25
var c3 = cube(3);             // 27
var cl1 = clamp(15, 0, 10);  // 10 (above max)
var cl2 = clamp(-5, 0, 10);  // 0  (below min)
var cl3 = clamp(7, 0, 10);   // 7  (within range)
var fm = fma(3, 4, 5);       // 17

// Result used in larger expression
var expr = square(3) + square(4);  // 9 + 16 = 25

// Nested inline calls
var nested = double_val(square(4));  // double_val(16) = 32

// Used in a loop — the key performance scenario for inlining
var sum_sq = 0;
for (var i = 1; i <= 10; i++) {
    sum_sq += square(i);
}
// 1+4+9+16+25+36+49+64+81+100 = 385

// Multiple inline calls per statement
var combined = clamp(square(7), 0, 100) + fma(2, 3, 4);  // clamp(49,0,100) + 10 = 59

// Boolean-returning single-expression function
var pos1 = is_positive(42);   // true
var pos2 = is_positive(-1);   // false
var pos3 = is_positive(0);    // false

{ s5: s5, c3: c3, cl1: cl1, cl2: cl2, cl3: cl3, fm: fm, expr: expr, nested: nested, sum_sq: sum_sq, combined: combined, pos1: pos1, pos2: pos2, pos3: pos3 };
