// Invariant: data → delete → accessor sequence on the same key.
// IS_ACCESSOR shape flag must be set fresh; previous data must not leak.

var obj = { foo: 7 };
console.log(obj.foo);
delete obj.foo;
console.log(obj.foo === undefined);
Object.defineProperty(obj, "foo", { get: function() { return 21; }, configurable: true });
console.log(obj.foo);
console.log("OK");
