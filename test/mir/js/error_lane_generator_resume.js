var f0 = function () {};
var f1 = function () {};
var f2 = function () {};
var f3 = function () {};
var f4 = function () {};
var f5 = function () {};
var f6 = function () {};
var f7 = function () {};
var f8 = function () {};
var f9 = function () {};
var f10 = function () {};
var f11 = function () {};
var f12 = function () {};
var f13 = function () {};
var f14 = function () {};
var f15 = function () {};
var x = "b";
var C = class {
  *m() { return 42; }
  [x] = 42;
  [10] = "meep";
  ["not initialized"];
};
var c = new C();
c.m().next();
