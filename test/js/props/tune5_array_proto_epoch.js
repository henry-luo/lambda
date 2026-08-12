// Tune5 prototype-epoch lock: a hole fast path is valid only while the
// receiver-selected intrinsic chain remains unchanged.
let hole = new Array(1);
console.log(0 in hole, hole[0]);
Object.prototype[0] = 71;
console.log(0 in hole, hole[0]);
delete Object.prototype[0];
console.log(0 in hole, hole[0]);

let assigned = 0;
Object.defineProperty(Array.prototype, "1", {
  set(v) { assigned = v; }, configurable: true
});
let withSetter = new Array(2);
withSetter[1] = 8;
console.log(assigned, Object.hasOwn(withSetter, "1"), 1 in withSetter);
delete Array.prototype[1];
withSetter[1] = 9;
console.log(withSetter[1], Object.hasOwn(withSetter, "1"), 1 in withSetter);
