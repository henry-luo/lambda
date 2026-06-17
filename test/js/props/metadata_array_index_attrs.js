function ok(value) {
  console.log(value ? "OK" : "FAIL");
}

function hasName(obj, name) {
  return Object.getOwnPropertyNames(obj).indexOf(name) >= 0;
}

var a = ["zero", "one", "two"];
Object.defineProperty(a, "1", {
  value: "locked",
  writable: false,
  enumerable: false,
  configurable: false
});

a[1] = "changed";
var d = Object.getOwnPropertyDescriptor(a, "1");
ok(a[1] === "locked");
ok(d.value === "locked" && d.writable === false &&
   d.enumerable === false && d.configurable === false);
ok(Object.keys(a).join(",") === "0,2");
ok(Object.getOwnPropertyNames(a).join(",") === "0,1,2,length");
ok(!hasName(a, "__nw_1") && !hasName(a, "__ne_1") && !hasName(a, "__nc_1"));
ok(delete a[1] === false && a[1] === "locked");

a.__nw_1 = "user-nw";
a.__ne_1 = "user-ne";
a.__nc_1 = "user-nc";
d = Object.getOwnPropertyDescriptor(a, "1");
ok(d.writable === false && d.enumerable === false && d.configurable === false);
ok(Object.keys(a).indexOf("__nw_1") >= 0 &&
   Object.keys(a).indexOf("__ne_1") >= 0 &&
   Object.keys(a).indexOf("__nc_1") >= 0);

var b = ["a"];
Object.defineProperty(b, "0", {
  value: "x",
  writable: false,
  configurable: true
});
Object.defineProperty(b, "0", {
  value: "y",
  writable: true,
  enumerable: true,
  configurable: true
});
b[0] = "z";
d = Object.getOwnPropertyDescriptor(b, "0");
ok(b[0] === "z" && d.value === "z" && d.writable === true &&
   d.enumerable === true && d.configurable === true);
ok(!hasName(b, "__nw_0") && !hasName(b, "__ne_0") && !hasName(b, "__nc_0"));
