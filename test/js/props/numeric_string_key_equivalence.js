// Invariant: numeric and numeric-string keys refer to the same slot.
// ToPropertyKey(1) must produce the same canonical key as "1".

var obj = {};
obj[1] = "a";
console.log(obj[1]);
console.log(obj["1"]);
obj["1"] = "b";
console.log(obj[1]);
console.log(obj["1"]);
delete obj[1];
console.log(obj["1"] === undefined);
console.log("OK");
