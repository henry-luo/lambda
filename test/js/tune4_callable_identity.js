console.log(String.prototype.trimStart === String.prototype.trimLeft);
console.log(String.prototype.trimEnd === String.prototype.trimRight);
console.log(Date.prototype.toUTCString === Date.prototype.toGMTString);
console.log(Array.prototype.values === Array.prototype[Symbol.iterator]);
console.log(Map.prototype.entries === Map.prototype[Symbol.iterator]);
console.log(Set.prototype.values === Set.prototype.keys);
console.log(Set.prototype.values === Set.prototype[Symbol.iterator]);

// Sharing one semantic target does not make distinct bindings identical.
console.log(Object.prototype.toLocaleString === Number.prototype.toLocaleString);
console.log(Object.prototype.toLocaleString === Date.prototype.toLocaleString);
console.log(Array.prototype.map === Uint8Array.prototype.map);

var saved = Math.abs;
Math.abs = function () { return 91; };
console.log(Math.abs(-4));
Math.abs = saved;
console.log(Math.abs(-4));

function makeCallableIdentityProbe() {
  return function () {};
}
var firstProbe = makeCallableIdentityProbe();
firstProbe.targetTest = 0;
var secondProbe = makeCallableIdentityProbe();
secondProbe.targetTest = function () { return true; };
console.log(firstProbe !== secondProbe);
console.log(typeof secondProbe.targetTest);
