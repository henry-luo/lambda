// A unique Symbol key must not collide with a same-spelling static property
// while defining a property on a built-in function object.
const fresh = Symbol("dispose");
console.log("missing:" + (Object.getOwnPropertyDescriptor(Symbol, fresh) === undefined));

Object.defineProperty(Symbol, fresh, {
  value: 123,
  writable: true,
  enumerable: false,
  configurable: true
});
let descriptor = Object.getOwnPropertyDescriptor(Symbol, fresh);
console.log("defined:" + Object.hasOwn(Symbol, fresh) + "," +
  (descriptor.value === 123) + "," + descriptor.configurable);
console.log("wellKnownIntact:" +
  (Object.getOwnPropertyDescriptor(Symbol, "dispose").configurable === false));

Object.defineProperty(Symbol, fresh, { value: 456 });
descriptor = Object.getOwnPropertyDescriptor(Symbol, fresh);
console.log("redefined:" + (descriptor.value === 456) + "," +
  (Object.getOwnPropertySymbols(Symbol).indexOf(fresh) >= 0));
console.log("OK");
