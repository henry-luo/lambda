// Invariant: delete on FLOAT key must canonicalize to property key
// so the next defineProperty for the same key does not read the
// deleted sentinel as a JsAccessorPair*.
// Regression: SIGSEGV at 0x400dead00dead08, fixed by canonicalizing
// FLOAT keys in js_delete_property.

var obj = {};
Object.defineProperty(obj, "1.5", { get: function() { return 99; }, configurable: true });
console.log(obj[1.5]);
delete obj[1.5];
console.log(obj[1.5]);
// Define an accessor again on the same key — must not read sentinel as pair.
Object.defineProperty(obj, "1.5", { get: function() { return 42; }, configurable: true });
console.log(obj[1.5]);
console.log("OK");
