// Tune5 P0/P1 behavior locks: key boundaries, receiver identity, and the
// DefineOwn/HasProperty separation required by S#7.1.14 and D6.2.2v2.
let a = [];
let names = ["0", "1", "4294967294", "4294967295", "00", "01", "-0", "1.0"];
for (let i = 0; i < names.length; i++) a[names[i]] = names[i];
console.log(a.length, 0 in a, 1 in a, a[0], a[1], Object.keys(a).join(","));

let proto = {
  get value() { return this.marker; },
  set assigned(v) { this.marker = v; },
  set defined(v) { throw new Error("inherited setter must not run"); },
  get hidden() { throw new Error("HasProperty must not get"); }
};
let target = Object.create(proto);
let receiver = { marker: 17 };
console.log(Reflect.get(target, "value", receiver));
console.log(Reflect.set(target, "assigned", 23, receiver), receiver.marker,
            Object.prototype.hasOwnProperty.call(target, "assigned"));
Object.defineProperty(target, "defined", {
  value: 31, writable: true, enumerable: true, configurable: true
});
console.log(target.defined, Object.prototype.hasOwnProperty.call(target, "defined"));
console.log("hidden" in target,
            Object.prototype.hasOwnProperty.call(target, "hidden"));

let seen = [];
let proxy = new Proxy({}, {
  get(t, key, recv) { seen.push("g:" + String(key) + ":" + (recv === proxy)); return 1; },
  set(t, key, value, recv) { seen.push("s:" + String(key) + ":" + (recv === proxy)); return true; }
});
proxy["4294967295"];
Reflect.set(proxy, "x", 2, proxy);
console.log(seen.join(","));

let protoHole = new Array(1);
Object.prototype[0] = 77;
console.log(0 in protoHole, protoHole[0]);
delete Object.prototype[0];
console.log(0 in protoHole, protoHole[0]);

let numeric = [1, 2, 3];
let numericAlias = numeric;
numeric[1] = 4;
numeric.push(5);
numeric.named = 9;
console.log(numeric.length, numeric[0], numeric[1], numeric[3], numeric.named,
            numeric === numericAlias);
numeric[2] = undefined;
console.log(numeric[2], numeric.length, numeric === numericAlias);
let holey = [1, 2];
let holeyAlias = holey;
console.log(delete holey[0], holey[0], 0 in holey,
            Object.keys(holey).join(","), holey === holeyAlias);

function strictWrite() {
  "use strict";
  let blocked = Object.preventExtensions({});
  let key = { toString() { return "computed"; } };
  try { blocked[key] = 1; return "no-throw"; }
  catch (e) { return "caught"; }
}
function strictNamed() {
  "use strict";
  let blocked = Object.preventExtensions({});
  try { blocked.named = 1; return "no-throw"; }
  catch (e) { return "caught"; }
}
console.log(strictWrite(), strictNamed());
