// JSI7: `return f()` to the running function is an ordinary call. The AST
// interpreter used to reuse its activation instead, which was observable.

// A try around a self-call observes the callee's throw.
function catchInner(n) {
    try {
        if (n === 3) throw new Error("boom at " + n);
        return catchInner(n + 1);
    } catch (e) {
        return "caught '" + e.message + "' in activation " + n;
    }
}
console.log(catchInner(0));

// Each activation's finally runs after its callee returns, innermost first.
const order = [];
function finallyOrder(n) {
    try {
        if (n === 2) {
            order.push("body" + n);
            return n;
        }
        return finallyOrder(n + 1);
    } finally {
        order.push("finally" + n);
    }
}
console.log(finallyOrder(0), order.join(","));

// Unbounded self-recursion in tail position throws RangeError (no TCO).
function explode(n) { return explode(n + 1); }
try {
    explode(0);
    console.log("explode returned");
} catch (e) {
    console.log("explode:", e instanceof RangeError, e.message);
}

// The same, caught inside the recursion itself: the deepest try catches.
function recoverInside(n) {
    try {
        return recoverInside(n + 1);
    } catch (e) {
        return e instanceof RangeError && n > 0;
    }
}
console.log("recover inside:", recoverInside(0));

// Bounded self-tail recursion still returns its value.
function sumTo(n, acc) { return n === 0 ? acc : sumTo(n - 1, acc + n); }
function countDown(n, acc) {
    if (n === 0) return acc;
    return countDown(n - 1, acc + 1);
}
console.log("bounded:", sumTo(1000, 0), countDown(1000, 0));

// Every self-call keeps its own arguments object, rest list and closures.
function argsDepth(n) {
    if (n === 0) return arguments.length;
    return argsDepth(n - 1, "a", "b");
}
function restTail(n, ...rest) {
    if (n === 0) return rest.join("|");
    return restTail(n - 1, n, ...rest);
}
const captured = [];
function closures(n) {
    captured.push(() => n);
    if (n === 0) return captured.map((f) => f()).join(",");
    return closures(n - 1);
}
console.log("activations:", argsDepth(2), restTail(3), closures(3));

// A strict self-call passes an undefined receiver.
function strictReceiver(n) {
    "use strict";
    if (n === 0) return this === undefined;
    return strictReceiver(n - 1);
}
console.log("strict this:", strictReceiver.call({}, 2));
