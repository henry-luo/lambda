// Invariant: negative INT keys canonicalize so set/delete refer to
// the same shape entry. Regression context: js_delete_property had
// to canonicalize INT keys; ensure it works for negative ints too.

var obj = {};
obj[-1] = "neg-one";
console.log(obj[-1]);
console.log(obj["-1"]);
console.log("hasOwn -1:", Object.prototype.hasOwnProperty.call(obj, "-1"));
delete obj[-1];
console.log("after delete:", obj[-1]);
console.log("hasOwn -1:", Object.prototype.hasOwnProperty.call(obj, "-1"));
console.log("OK");
