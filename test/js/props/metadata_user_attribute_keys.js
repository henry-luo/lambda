function hasName(obj, name) {
  var names = Object.getOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    if (names[i] === name) return true;
  }
  return false;
}

var o = {};
o.__nw_alpha = 1;
o.__ne_alpha = 2;
o.__nc_alpha = 3;
console.log("values:" + o.__nw_alpha + "," + o.__ne_alpha + "," + o.__nc_alpha);
console.log("keys:" + Object.keys(o).join(","));
console.log("names:" +
  hasName(o, "__nw_alpha") + "," +
  hasName(o, "__ne_alpha") + "," +
  hasName(o, "__nc_alpha"));

Object.defineProperty(o, "locked", {
  value: 10,
  writable: false,
  enumerable: false,
  configurable: false
});
o.__nw_locked = 11;
o.__ne_locked = 12;
o.__nc_locked = 13;
var locked = Object.getOwnPropertyDescriptor(o, "locked");
var marker = Object.getOwnPropertyDescriptor(o, "__nw_locked");
console.log("locked:" +
  o.locked + "," +
  locked.writable + "," +
  locked.enumerable + "," +
  locked.configurable);
console.log("marker:" +
  o.__nw_locked + "," +
  o.__ne_locked + "," +
  o.__nc_locked + "," +
  marker.writable + "," +
  marker.enumerable + "," +
  marker.configurable);

Object.defineProperty(o, "__nw_readonly", {
  value: 21,
  writable: false,
  enumerable: true,
  configurable: true
});
o.__nw_readonly = 22;
console.log("markerReadonly:" + o.__nw_readonly);

var arr = [];
arr.foo = 1;
arr.__nw_foo = 2;
arr.foo = 3;
console.log("arrayNamed:" + arr.foo + "," + arr.__nw_foo + "," + Object.keys(arr).join(","));

delete o.__nw_alpha;
console.log("deleteUser:" + hasName(o, "__nw_alpha") + "," + hasName(o, "__ne_alpha"));
console.log("OK");
