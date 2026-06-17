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

function f() {}
Object.defineProperty(f, "hidden", {
  value: 7,
  writable: true,
  enumerable: false,
  configurable: true
});
f.visible = 9;

console.log("fnEnumerable:" +
  Object.prototype.propertyIsEnumerable.call(f, "hidden") + "," +
  Object.prototype.propertyIsEnumerable.call(f, "visible"));
console.log("fnKeys:" + hasKey(f, "hidden") + "," + hasKey(f, "visible"));
console.log("fnNames:" + hasName(f, "hidden") + "," + hasName(f, "__ne_hidden"));

f.__ne_hidden = 11;
console.log("markerUser:" + f.__ne_hidden + "," +
  Object.prototype.propertyIsEnumerable.call(f, "__ne_hidden") + "," +
  hasKey(f, "__ne_hidden"));

Object.defineProperty(f, "hidden", { enumerable: true });
console.log("fnAfter:" +
  Object.prototype.propertyIsEnumerable.call(f, "hidden") + "," +
  hasKey(f, "hidden"));
console.log("OK");
