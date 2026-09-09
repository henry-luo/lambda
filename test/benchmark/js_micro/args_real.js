// A function that genuinely uses the arguments object, so the per-call cost of
// materializing it is the whole measurement (JS_Tune10 T10-4).
function f(a, b) { let s = 0; for (let i = 0; i < arguments.length; i++) s += arguments[i]; return s; }
let t = Date.now(); let acc = 0;
for (let i = 0; i < 200000; i++) acc += f(i, 1);
console.log("arguments 200k calls:", Date.now() - t, "ms", acc);
