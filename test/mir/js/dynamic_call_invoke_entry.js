var target = function (a, b) { return a + b; };
function callDyn(f, x) {
    return f(x, 1);
}
console.log(callDyn(target, 2));
console.log(callDyn(Math.max, 3));
