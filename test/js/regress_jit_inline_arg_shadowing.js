// Regression: when an inlinable single-return function (P6 inliner) is called with an
// argument that references a free variable whose name matches one of the callee's
// parameters, that free variable must resolve to the OUTER scope — not to the
// just-bound inline parameter. The inliner used to bind params before evaluating later
// args, shadowing the free variable, which made jetstream "crypto-md5" hash wrong.
function add2(x, y) { return (x & 255) + (y & 255); }
var x = 100;
// inner add2(x,0) = 100; outer add2(7,100) = 107 (bug produced 14: inner saw x=7)
console.log("r1=" + add2(7, add2(x, 0)));

function mix(a, x, t) { return add2(add2(a, t), x); }
var a = 5, t = 9;
// add2(a,t)=14; outer add2(14, x=200)=214 (bug produced 28: 2nd arg x saw the bound 14)
console.log("r2=" + mix(a, 200, t));
