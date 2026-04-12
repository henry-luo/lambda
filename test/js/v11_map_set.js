// Map basic operations
let m = new Map();
m.set("a", 1);
m.set("b", 2);
console.log(m.get("a"));
console.log(m.get("b"));
console.log(m.has("a"));
console.log(m.has("c"));
console.log(m.size);

// Map delete
m.delete("a");
console.log(m.has("a"));
console.log(m.size);

// Map clear
m.set("x", 10);
m.clear();
console.log(m.size);

// Map with numeric keys
let m2 = new Map();
m2.set(1, "one");
m2.set(2, "two");
console.log(m2.get(1));
console.log(m2.get(2));

// Map keys/values/entries
let m3 = new Map();
m3.set("k1", "v1");
m3.set("k2", "v2");
let keys = Array.from(m3.keys());
console.log(keys.length);
let vals = Array.from(m3.values());
console.log(vals.length);
let entries = Array.from(m3.entries());
console.log(entries.length);
console.log(entries[0].length);

// Map forEach
let sum = 0;
let m4 = new Map();
m4.set("a", 10);
m4.set("b", 20);
m4.forEach(function(v, k) { sum = sum + v; });
console.log(sum);

// Map chaining
let m5 = new Map();
m5.set("x", 1).set("y", 2);
console.log(m5.get("x"));
console.log(m5.get("y"));

// Set basic operations
let s = new Set();
s.add(1);
s.add(2);
s.add(3);
s.add(2); // duplicate
console.log(s.size);
console.log(s.has(1));
console.log(s.has(4));

// Set delete
s.delete(2);
console.log(s.size);
console.log(s.has(2));

// Set clear
s.clear();
console.log(s.size);

// Set values
let s2 = new Set();
s2.add("a");
s2.add("b");
let sv = Array.from(s2.values());
console.log(sv.length);

// Set forEach
let items = [];
let s3 = new Set();
s3.add(10);
s3.add(20);
s3.forEach(function(v) { items.push(v); });
console.log(items.length);
