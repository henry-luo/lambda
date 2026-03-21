// v11: Nullish/Logical assignment operators (??=, &&=, ||=)

// ??= (nullish assignment)
let a = null;
a ??= 42;
console.log("null ??= 42:", a);

let b = 0;
b ??= 99;
console.log("0 ??= 99:", b);

let c = "hello";
c ??= "world";
console.log("hello ??= world:", c);

// ||= (logical or assignment)
let d = 0;
d ||= 10;
console.log("0 ||= 10:", d);

let e = "keep";
e ||= "replace";
console.log("keep ||= replace:", e);

let f = null;
f ||= "fallback";
console.log("null ||= fallback:", f);

// &&= (logical and assignment)
let g = 1;
g &&= 42;
console.log("1 &&= 42:", g);

let h = 0;
h &&= 99;
console.log("0 &&= 99:", h);

let i = "yes";
i &&= "no";
console.log("yes &&= no:", i);
