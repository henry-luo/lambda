function ok(value) {
  console.log(value ? "OK" : "FAIL");
}

function hasOwn(obj, key) {
  return Object.prototype.hasOwnProperty.call(obj, key);
}

var a = [1, 2, 3];
Object.defineProperty(a, "length", { writable: false });

var d = Object.getOwnPropertyDescriptor(a, "length");
ok(d.value === 3 && d.writable === false &&
   d.enumerable === false && d.configurable === false);

a.length = 1;
ok(a.length === 3 && a[2] === 3);

a[3] = 4;
ok(a.length === 3 && !hasOwn(a, "3"));

var rejected = false;
try {
  Object.defineProperty(a, "3", { value: 4 });
} catch (e) {
  rejected = e && e.name === "TypeError";
}
ok(rejected && a.length === 3 && !("3" in a));

var names = Object.getOwnPropertyNames(a);
var lengthCount = 0;
for (var i = 0; i < names.length; i++) {
  if (names[i] === "length") lengthCount++;
}
ok(lengthCount === 1);
ok(names.indexOf("__nw_length") === -1);

a.__nw_length = "user";
ok(a.__nw_length === "user" && Object.keys(a).indexOf("__nw_length") >= 0);

d = Object.getOwnPropertyDescriptor(a, "length");
ok(d.writable === false && d.enumerable === false && d.configurable === false);
