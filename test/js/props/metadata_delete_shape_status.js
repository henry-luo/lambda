function ok(value) {
  console.log(value ? "OK" : "FAIL");
}

function hasName(obj, name) {
  return Object.getOwnPropertyNames(obj).indexOf(name) >= 0;
}

function hasKey(obj, name) {
  return Object.keys(obj).indexOf(name) >= 0;
}

var o = {};
Object.defineProperty(o, "a", {
  value: 1,
  writable: true,
  enumerable: true,
  configurable: true
});
delete o.a;
ok(!o.hasOwnProperty("a") && !("a" in o) && !hasName(o, "a") && !hasKey(o, "a"));
Object.defineProperty(o, "a", {
  value: 2,
  writable: true,
  enumerable: true,
  configurable: true
});
ok(o.a === 2 && o.hasOwnProperty("a") && hasKey(o, "a"));

var proto = { x: 7 };
var child = Object.create(proto);
Object.defineProperty(child, "x", {
  value: 1,
  writable: true,
  enumerable: true,
  configurable: true
});
delete child.x;
ok(child.x === 7 && ("x" in child) && !child.hasOwnProperty("x"));

function f() {}
f.custom = 1;
delete f.custom;
ok(!f.hasOwnProperty("custom") && !hasName(f, "custom") && !hasKey(f, "custom"));
f.custom = 2;
ok(f.custom === 2 && Object.prototype.propertyIsEnumerable.call(f, "custom"));

var a = [];
Object.defineProperty(a, "2", {
  value: "two",
  writable: true,
  enumerable: true,
  configurable: true
});
delete a[2];
ok(!a.hasOwnProperty("2") && !hasName(a, "2") && !hasKey(a, "2"));
Object.defineProperty(a, "2", {
  value: "new",
  writable: true,
  enumerable: true,
  configurable: true
});
ok(a[2] === "new" && hasKey(a, "2"));
