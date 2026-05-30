// Invariant: redefining `Object.prototype.__proto__` as a user accessor
// does NOT corrupt prototype-chain traversal. Internal [[GetPrototypeOf]]
// is independent of the magic accessor; reads through `js_get_prototype`
// must not return the raw `JsAccessorPair*` slot value as if it were a
// prototype map (would bus-error on chain walk).
//
// Regression: language/expressions/class/poisoned-underscore-proto.js
// caused intermittent SIGBUS in batches when js_get_prototype read the
// accessor pair slot directly. Fixed in js_get_prototype by skipping
// IS_ACCESSOR slots (returning ItemNull as the safe fallback).

function Test262Error(m){ this.message = m; }
Object.defineProperty(Object.prototype, '__proto__', {
    set: function() { throw new Test262Error('should not be called'); }
});

// class extends Array triggers prototype-chain operations during construction.
var A = class extends Array {};
console.log(new A(1).length);

// Plain object construction must also still work.
var o = {};
o.x = 5;
console.log(o.x);

console.log("OK");
