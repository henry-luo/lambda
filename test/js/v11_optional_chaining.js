// v11: Optional chaining (?.)

// Basic optional chaining on null/undefined
let obj = { a: { b: { c: 42 } } };
console.log("deep:", obj.a.b.c);
console.log("opt deep:", obj?.a?.b?.c);

// Optional on null
let x = null;
console.log("null?.prop:", x?.name);

// Optional on undefined
let y = undefined;
console.log("undef?.prop:", y?.foo);

// Optional on valid object
let person = { name: "Alice", age: 30 };
console.log("person?.name:", person?.name);
console.log("person?.missing:", person?.missing);

// Optional with computed property
let arr = [10, 20, 30];
let empty = null;
console.log("arr?.[1]:", arr?.[1]);
console.log("null?.[0]:", empty?.[0]);
