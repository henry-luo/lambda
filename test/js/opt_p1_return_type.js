// P1: Return type -> variable type propagation
// When a function has a known INT/FLOAT return type, the result stored in
// a variable is treated with the correct type for subsequent arithmetic,
// avoiding unnecessary box/unbox round-trips.

function square(x) { return x * x; }
function add_one(x) { return x + 1; }
function times_three(x) { return x * 3; }

// INT return -> var INT -> arithmetic
var s = square(6);        // 36
var a = s + 4;            // 40
var b = s * 2;            // 72
var d = s - 6;            // 30

// Return value used in further function calls
var t = times_three(s);   // 108

// Chained int-returning calls
var ch = add_one(add_one(add_one(10)));  // 13

// Return value used in compound expression (both sides)
var combo = square(3) + square(4) + square(5);  // 9 + 16 + 25 = 50

// Return value used as function argument
var nested = square(add_one(4));  // square(5) = 25

// Result in a loop accumulator
var loop_sum = 0;
for (var i = 1; i <= 5; i++) {
    var sq_i = square(i);
    loop_sum = loop_sum + sq_i;
}
// 1 + 4 + 9 + 16 + 25 = 55

{ a: a, b: b, d: d, t: t, ch: ch, combo: combo, nested: nested, loop_sum: loop_sum };
