// v9 Object methods: Object.values, Object.entries, Object.assign, hasOwnProperty, freeze/isFrozen

// Object.values
let obj1 = { a: 1, b: 2, c: 3 };
let vals = Object.values(obj1);
console.log("values:", vals.join(","));

// Object.entries
let obj2 = { x: 10, y: 20 };
let entries = Object.entries(obj2);
console.log("entries len:", entries.length);
console.log("entry 0:", entries[0][0], entries[0][1]);
console.log("entry 1:", entries[1][0], entries[1][1]);

// Object.assign
let target = { a: 1 };
let source = { b: 2, c: 3 };
let result = Object.assign(target, source);
console.log("assign a:", result.a, "b:", result.b, "c:", result.c);

// hasOwnProperty
let obj3 = { name: "test", value: 42 };
console.log("hasOwn name:", obj3.hasOwnProperty("name"));
console.log("hasOwn missing:", obj3.hasOwnProperty("missing"));

// Object.freeze and Object.isFrozen
let obj4 = { x: 1 };
console.log("before freeze:", Object.isFrozen(obj4));
Object.freeze(obj4);
console.log("after freeze:", Object.isFrozen(obj4));
