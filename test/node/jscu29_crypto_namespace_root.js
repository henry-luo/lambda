// JSCU29: crypto namespace uses its shared rooted realm slot.
var first = require("crypto");
gc();
var second = require("crypto");
console.log(first === second, typeof second.createHash);
