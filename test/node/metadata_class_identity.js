function hasName(obj, name) {
  var names = Object.getOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    if (names[i] === name) return true;
  }
  return false;
}

var types = require("util").types;

var fake = {};
fake.__class_name__ = "RegExp";
console.log("fakeKeys:" + Object.keys(fake).join(","));
console.log("fakeTag:" + Object.prototype.toString.call(fake));
console.log("fakeInstance:" + (fake instanceof RegExp));
console.log("fakeUtil:" + types.isRegExp(fake));

var re = /a/g;
console.log("regexOwnMarker:" + hasName(re, "__class_name__"));
console.log("regexBrand:" +
  Object.prototype.toString.call(re) + "," +
  (re instanceof RegExp) + "," +
  types.isRegExp(re));

var date = new Date(0);
console.log("dateOwnMarker:" + hasName(date, "__class_name__"));
console.log("dateBrand:" +
  Object.prototype.toString.call(date) + "," +
  (date instanceof Date) + "," +
  types.isDate(date));

class C {
  m() { return 3; }
}
var c = new C();
console.log("classMarkers:" +
  hasName(C, "__class_name__") + "," +
  hasName(C.prototype, "__class_name__") + "," +
  hasName(c, "__class_name__"));
console.log("classIdentity:" + (c instanceof C) + "," + c.m() + "," + (c.constructor === C));
console.log("classFunctionIdentity:" +
  (C instanceof Function) + "," +
  (C.toString().indexOf("class C") >= 0) + "," +
  (Function.prototype.toString.call(C).indexOf("class C") >= 0));

class A extends Array {}
var a = new A();
a.push(1);
console.log("arraySubclass:" +
  (a instanceof A) + "," +
  (a instanceof Array) + "," +
  Array.isArray(a) + "," +
  a.length + "," +
  a[0]);

var n = new Number(1);
console.log("wrapper:" +
  hasName(n, "__class_name__") + "," +
  Object.prototype.toString.call(n) + "," +
  types.isNumberObject(n));

delete String.prototype.toString;
console.log("stringPrototypeAfterDelete:" +
  Object.prototype.toString.call(String.prototype) + "," +
  String.prototype.toString());

delete Number.prototype.toString;
console.log("numberPrototypeAfterDelete:" +
  Object.prototype.toString.call(Number.prototype) + "," +
  Number.prototype.toString());

console.log("OK");
