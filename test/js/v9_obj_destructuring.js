// v9 Object destructuring

// Basic object destructuring
const { a, b, c } = { a: 1, b: 2, c: 3 };
console.log("basic:", a, b, c);

// Destructuring with rename
const { x: px, y: py } = { x: 10, y: 20 };
console.log("rename:", px, py);

// Object destructuring in for-of
const people = [{ name: "Alice", age: 30 }, { name: "Bob", age: 25 }];
for (const { name, age } of people) {
    console.log("person:", name, age);
}

// delete operator
let obj = { a: 1, b: 2, c: 3 };
delete obj.b;
console.log("after delete a:", obj.a, "c:", obj.c);
