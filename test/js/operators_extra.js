// Extra operators tests: compound assignment, ++/--, typeof, **, ??

// Compound assignment operators
var a = 10;
a += 5;
var b = 20;
b -= 7;
var c = 6;
c *= 4;
var d = 100;
d /= 5;
var e = 17;
e %= 5;

// Postfix increment/decrement
var f = 10;
f++;
var g = 10;
g--;

// Prefix increment/decrement
var h = 5;
var preInc = ++h;
var j = 5;
var preDec = --j;

// typeof operator
var typeNum = typeof 42;
var typeStr = typeof "hello";
var typeBool = typeof true;
var typeNull = typeof null;
var typeUndef = typeof undefined;

// Exponentiation
var pow1 = 2 ** 10;
var pow2 = 3 ** 3;
var pow3 = 10 ** 0;

// Nullish coalescing
var nc1 = null ?? "fallback";
var nc2 = undefined ?? "default";
var nc3 = 0 ?? "not this";
var nc4 = "" ?? "not this either";
var nc5 = false ?? "nope";

// Logical OR (short-circuit)
var or1 = false || "yes";
var or2 = 0 || 42;
var or3 = "hello" || "world";

const result = {
  plusEq: a,
  minusEq: b,
  mulEq: c,
  divEq: d,
  modEq: e,
  postInc: f,
  postDec: g,
  preInc: preInc,
  hVal: h,
  preDec: preDec,
  jVal: j,
  typeNum: typeNum,
  typeStr: typeStr,
  typeBool: typeBool,
  typeNull: typeNull,
  typeUndef: typeUndef,
  pow1: pow1,
  pow2: pow2,
  pow3: pow3,
  nc1: nc1,
  nc2: nc2,
  nc3: nc3,
  nc4: nc4,
  nc5: nc5,
  or1: or1,
  or2: or2,
  or3: or3
};
result;
