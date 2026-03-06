// Object static methods tests

// Object.keys on simple object
var obj1 = { a: 1, b: 2, c: 3 };
var keys1 = Object.keys(obj1);
var k1_len = keys1.length;
var k1_0 = keys1[0];
var k1_1 = keys1[1];
var k1_2 = keys1[2];

// Object.keys on empty object
var obj2 = {};
var keys2 = Object.keys(obj2);
var k2_len = keys2.length;

// Object.keys used in for-of
var obj3 = { x: 10, y: 20 };
var keyStr = "";
var k3 = Object.keys(obj3);
for (var k of k3) {
  keyStr = keyStr + k + ",";
}

const result = {
  k1_len: k1_len,
  k1_0: k1_0,
  k1_1: k1_1,
  k1_2: k1_2,
  k2_len: k2_len,
  keyStr: keyStr
};
result;
