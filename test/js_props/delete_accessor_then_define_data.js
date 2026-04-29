// Invariant: accessor → delete → data sequence on the same key.
// IS_ACCESSOR shape flag must be cleared so the new data slot is
// not interpreted as a JsAccessorPair*.

var obj = {};
Object.defineProperty(obj, "x", { get: function() { return 11; }, configurable: true });
console.log(obj.x);
delete obj.x;
console.log(obj.x === undefined);
obj.x = 5;
console.log(obj.x);
console.log(typeof obj.x);
console.log("OK");
