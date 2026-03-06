// Phase 5e: Closure Tests
// Tests closure capture, mutable captures, composition,
// closures as callbacks, arrow closures, and loop captures.

// --- Basic closure: captured parameter ---
function makeAdder(x) {
    return function(y) {
        return x + y;
    };
}
const add5 = makeAdder(5);
const add10 = makeAdder(10);
console.log("adder:", add5(3), add10(3));

// --- Multiple captured variables ---
function multiCapture(x, y, z) {
    return function() {
        return x * 100 + y * 10 + z;
    };
}
console.log("multi:", multiCapture(4, 5, 6)());

// --- Mutable capture: accumulator ---
function makeAccumulator(initial) {
    let total = initial;
    return function(amount) {
        total = total + amount;
        return total;
    };
}
const acc = makeAccumulator(0);
console.log("accum:", acc(5), acc(10), acc(3));

// --- Closure as callback ---
function applyTwice(fn, val) {
    return fn(fn(val));
}
const addOne = function(n) { return n + 1; };
console.log("callback:", applyTwice(addOne, 5));

// --- Arrow function closure ---
function makeScaler(factor) {
    return (x) => x * factor;
}
const dbl = makeScaler(2);
const quad = makeScaler(4);
console.log("arrow:", dbl(7), quad(7));

// --- Function composition ---
function compose(f, g) {
    return function(x) {
        return f(g(x));
    };
}
const inc = function(n) { return n + 1; };
const double = function(n) { return n * 2; };
const incThenDbl = compose(double, inc);
const dblThenInc = compose(inc, double);
console.log("compose:", incThenDbl(3), dblThenInc(3));

// --- Closure over loop variable ---
function makeSquarers() {
    const fns = [];
    for (let i = 0; i < 5; i = i + 1) {
        fns.push(function() { return i * i; });
    }
    return fns;
}
const squarers = makeSquarers();
console.log("loop:", squarers[0](), squarers[1](), squarers[2](), squarers[3](), squarers[4]());

// --- Closure as map/filter callback ---
function makeMultiplier(n) {
    return function(x) { return x * n; };
}
const nums = [1, 2, 3, 4, 5];
const tripled = nums.map(makeMultiplier(3));
console.log("map:", tripled[0], tripled[1], tripled[2], tripled[3], tripled[4]);

function makeThreshold(min) {
    return function(x) { return x > min; };
}
const big = nums.filter(makeThreshold(3));
console.log("filter:", big[0], big[1]);

// --- Closure captures let and mutates ---
function makeRunningSum() {
    let n = 0;
    return function(x) {
        n = n + x;
        return n;
    };
}
const rsum = makeRunningSum();
console.log("running:", rsum(1), rsum(2), rsum(3));

// --- Immediate use of returned closure ---
function makeGreeter(greeting) {
    return function(name) {
        return greeting + " " + name;
    };
}
console.log("immediate:", makeGreeter("Hello")("World"));
