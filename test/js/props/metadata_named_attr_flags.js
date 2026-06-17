function hasName(obj, name) {
  var names = Object.getOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    if (names[i] === name) return true;
  }
  return false;
}

function hasKey(obj, name) {
  var keys = Object.keys(obj);
  for (var i = 0; i < keys.length; i++) {
    if (keys[i] === name) return true;
  }
  return false;
}

var o = {};
Object.defineProperty(o, "locked", {
  value: 1,
  writable: false,
  enumerable: false,
  configurable: false
});
var d = Object.getOwnPropertyDescriptor(o, "locked");
console.log("locked:" + o.locked + "," + hasKey(o, "locked"));
console.log("flags:" + d.writable + "," + d.enumerable + "," + d.configurable);
console.log("markers:" +
  hasName(o, "__nw_locked") + "," +
  hasName(o, "__ne_locked") + "," +
  hasName(o, "__nc_locked"));
o.locked = 2;
console.log("afterAssign:" + o.locked);

var c = {};
Object.defineProperty(c, "x", {
  value: 1,
  writable: false,
  enumerable: true,
  configurable: true
});
Object.defineProperty(c, "x", {
  value: 2,
  writable: true
});
c.x = 3;
var cd = Object.getOwnPropertyDescriptor(c, "x");
console.log("redefine:" + c.x + "," + cd.writable + "," + cd.configurable);

var del = {};
Object.defineProperty(del, "x", {
  value: 1,
  writable: false,
  enumerable: true,
  configurable: true
});
console.log("delete:" + (delete del.x) + "," + hasName(del, "x"));
console.log("OK");
