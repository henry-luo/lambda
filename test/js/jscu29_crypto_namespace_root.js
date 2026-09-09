// JSCU29: crypto namespace uses its one rooted realm slot.
var first = require("crypto");
gc();
var second = require("crypto");
console.log(first === second, typeof second.createHash);
