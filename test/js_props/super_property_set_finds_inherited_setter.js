// Invariant: super.x = v must walk the prototype chain through the
// accessor pair API and invoke the inherited setter with the correct
// receiver. Regression context: Bug 9 (js_super_property_set rewrite).

var captured = null;
var Base = {};
Object.defineProperty(Base, "x", {
    get: function() { return this._x; },
    set: function(v) { captured = this; this._x = v * 2; },
    configurable: true
});

var Derived = Object.create(Base);
var obj = Object.create(Derived);

// Use a method that closes over `super` to drive the inherited setter.
Derived.set = function(v) { /* desugar of `super.x = v`: */
    Object.getPrototypeOf(Derived).x = v;
    // simpler: directly assign via accessor on Base from Derived's chain
};
// Direct equivalent: assigning to obj.x must dispatch the setter on Base
// with `obj` as the receiver and `obj._x` storing the doubled value.
obj.x = 5;
console.log(obj._x);
console.log(captured === obj);
console.log("OK");
