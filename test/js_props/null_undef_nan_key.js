// Invariant: null, undefined, NaN, Infinity as keys must canonicalize to
// "null", "undefined", "NaN", "Infinity".
var obj = {};
obj[null] = "n";
obj[undefined] = "u";
obj[NaN] = "x";
obj[Infinity] = "i";
console.log(obj["null"]);
console.log(obj["undefined"]);
console.log(obj["NaN"]);
console.log(obj["Infinity"]);
delete obj[null];
console.log(obj["null"] === undefined);
delete obj[NaN];
console.log(obj["NaN"] === undefined);
console.log("OK");
