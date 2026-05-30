// Invariant: get/set with boolean key canonicalizes to "true"/"false".
var obj = {};
obj[true] = "t";
obj[false] = "f";
console.log(obj["true"]);
console.log(obj["false"]);
console.log(obj[true]);
console.log(obj[false]);
delete obj[true];
console.log(obj["true"] === undefined);
console.log("OK");
