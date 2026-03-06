// For-in loop tests

// for-in over simple object
var keys1 = "";
var obj1 = { x: 10, y: 20, z: 30 };
for (var k in obj1) {
  keys1 = keys1 + k + ",";
}

// for-in with property access
var total = 0;
var prices = { apple: 3, banana: 2, cherry: 5 };
for (var item in prices) {
  total = total + prices[item];
}

// for-in counting keys
var keyCount = 0;
var person = { name: "Alice", age: 30, city: "NYC" };
for (var p in person) {
  keyCount = keyCount + 1;
}

const result = {
  keys: keys1,
  total: total,
  keyCount: keyCount
};
result;
