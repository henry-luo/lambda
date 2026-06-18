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

var oldPayload = 0xDEAD00DEAD00;
var collision = { oldPayload: oldPayload };
ok(collision.hasOwnProperty("oldPayload") && collision.oldPayload === oldPayload && hasKey(collision, "oldPayload"));

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

function vf(a, b) {}
delete vf.length;
ok(!vf.hasOwnProperty("length") && !hasName(vf, "length") && vf.length === 0);
Object.defineProperty(vf, "length", {
  value: 4,
  writable: true,
  enumerable: true,
  configurable: true
});
ok(vf.length === 4 && vf.hasOwnProperty("length") && hasKey(vf, "length"));

function nf() {}
delete nf.name;
ok(!nf.hasOwnProperty("name") && !hasName(nf, "name") && nf.name === undefined);
Object.defineProperty(nf, "name", {
  value: "revived",
  writable: true,
  enumerable: true,
  configurable: true
});
ok(nf.name === "revived" && nf.hasOwnProperty("name") && hasKey(nf, "name"));

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

var dense = [];
dense[0] = oldPayload;
ok(0 in dense && dense[0] === oldPayload);

var objProtoToStringDeleted = delete Object.prototype.toString;
ok(objProtoToStringDeleted && !Object.prototype.hasOwnProperty("toString") && !hasName(Object.prototype, "toString"));
ok(typeof Object.prototype.toString === "undefined" && typeof ({}).toString === "undefined");

var objProtoToStringDirectThrows = false;
try {
  Object.prototype.toString();
} catch (e) {
  objProtoToStringDirectThrows = e instanceof TypeError;
}
ok(objProtoToStringDirectThrows);

var objProtoToStringComputedThrows = false;
try {
  Object.prototype["toString"]();
} catch (e) {
  objProtoToStringComputedThrows = e instanceof TypeError;
}
ok(objProtoToStringComputedThrows);
