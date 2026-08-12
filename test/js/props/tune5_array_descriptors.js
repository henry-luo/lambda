// Tune5 descriptor-overlay lock: indexed attributes and accessors stay in the
// companion shape without corrupting dense element presence or enumeration.
let values = [10, 20];
Object.defineProperty(values, "0", {
  value: 42, writable: false, enumerable: false, configurable: false
});
console.log(values[0], Object.hasOwn(values, "0"),
            Object.getOwnPropertyDescriptor(values, "0").writable,
            Object.keys(values).join(","));
values[0] = 99;
console.log(values[0]);

let stored = 0;
let accessors = [1, 2];
Object.defineProperty(accessors, "1", {
  get() { return stored + 5; },
  set(v) { stored = v; }, enumerable: true, configurable: true
});
console.log(accessors[1], Object.hasOwn(accessors, "1"),
            Object.keys(accessors).join(","));
accessors[1] = 8;
console.log(stored, accessors[1]);

let fixedLength = [3, 4];
Object.defineProperty(fixedLength, "length", { writable: false });
try { fixedLength.length = 1; console.log("no-throw"); }
catch (e) { console.log("caught"); }
console.log(fixedLength.length, Object.getOwnPropertyDescriptor(
  fixedLength, "length").writable);
