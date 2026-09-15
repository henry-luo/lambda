// A `return` in a MIR native-version body is a completion: it runs enclosing
// finally blocks and closes open iterators before leaving, and a self-tail
// call is an ordinary call (no loop rewrite), so it neither skips those
// landings nor changes termination.
const log = [];

// finally after a native return, including a self-tail call inside try
function finallyNative(n) {
    try { if (n === 1) return n * 0.5; return n * 1.5; } finally { log.push("f" + n); }
}
function selfInTry(n) {
    try { if (n === 1) return n * 1.0; return selfInTry(n + 1); } finally { log.push("s" + n); }
}
console.log("finally:", finallyNative(1), finallyNative(2), selfInTry(0), log.splice(0).join(","));

// finally may override the returned value; nested finally blocks unwind in order
function overrideNative(n) { try { return n * 2; } finally { if (n > 10) return n * 3; } }
function nestedNative(n) {
    try { try { return n * 2; } finally { log.push("inner"); } } finally { log.push("outer"); }
}
function catchFinally(n) {
    try { if (n > 0) throw new Error("x"); return n * 2; } catch (e) { return n * 4; } finally { log.push("c" + n); }
}
console.log("routing:", overrideNative(2), overrideNative(20), nestedNative(3), catchFinally(0), catchFinally(1),
    log.splice(0).join(","));

// a return inside for-of closes the iterator
function iterable(tag) {
    let i = 0;
    return { [Symbol.iterator]() {
        return { next: () => ({ value: i++, done: false }), return: () => { log.push("close " + tag); return {}; } };
    } };
}
const source = iterable("float");
function firstFloat(n) { for (const x of source) { if (n > 0) return n * 0.5; } return 0.5; }
console.log("for-of:", firstFloat(3), log.splice(0).join(","));

// unbounded self-tail recursion throws RangeError instead of returning a value
function unbounded(n) { return unbounded(n + 1); }
function deep(n) { if (n > 1500000) return n * 1.0; return deep(n + 1); }
for (const [name, f] of [["unbounded", unbounded], ["deep", deep]]) {
    try { console.log(name, "returned", f(0)); } catch (e) { console.log(name, e instanceof RangeError); }
}

// bounded self-tail recursion still returns its value
function sumTo(n, acc) { if (n === 0) return acc * 1.0; return sumTo(n - 1, acc + n); }
console.log("bounded:", sumTo(1000, 0));
