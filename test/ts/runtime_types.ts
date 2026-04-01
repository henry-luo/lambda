// Runtime type introspection: type() and typeof

// type() returns TS-style type names
console.log(type(42));
console.log(type(3.14));
console.log(type("hello"));
console.log(type(true));
console.log(type(null));
console.log(type([1, 2, 3]));

// typeof returns standard JS/TS typeof strings
console.log(typeof 42);
console.log(typeof "hello");
console.log(typeof true);
console.log(typeof null);
console.log(typeof undefined);

// type() with typed function
function add(x: number, y: number): number { return x + y; }
console.log(type(add));

// typeof with function
console.log(typeof add);
