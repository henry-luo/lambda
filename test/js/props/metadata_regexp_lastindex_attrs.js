function hasName(obj, name) {
  var names = Object.getOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    if (names[i] === name) return true;
  }
  return false;
}

var r = /a/g;
Object.defineProperty(r, "lastIndex", {
  value: 2,
  writable: false,
  enumerable: false,
  configurable: false
});

var d = Object.getOwnPropertyDescriptor(r, "lastIndex");
console.log("before:" +
  d.value + "," +
  d.writable + "," +
  d.enumerable + "," +
  d.configurable + "," +
  hasName(r, "__nw_lastIndex"));

try {
  r.compile("b", "g");
  console.log("compile:ok");
} catch (e) {
  console.log("compile:" + e.name);
}

console.log("after:" + r.lastIndex + "," + hasName(r, "__nw_lastIndex"));

r.__nw_lastIndex = 1;
console.log("markerUser:" + r.__nw_lastIndex + "," +
  Object.prototype.propertyIsEnumerable.call(r, "__nw_lastIndex"));
console.log("OK");
