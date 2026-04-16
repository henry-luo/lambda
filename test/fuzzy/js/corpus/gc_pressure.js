// Stress: GC pressure — many short-lived objects, strings, closures
// Tests that GC handles rapid allocation/deallocation without corruption

// Object churn
for (var i = 0; i < 3000; i++) {
  var tmp = {a: i, b: [i, i+1], c: "str" + i};
}

// String concatenation growth
var s = "";
for (var i = 0; i < 2000; i++) {
  s = s + String.fromCharCode(65 + (i % 26));
}
s.length;

// Closure allocation churn
var closures = [];
for (var i = 0; i < 500; i++) {
  closures.push((function(x) { return function() { return x; }; })(i));
}
closures[0]();
closures[499]();
closures = null; // release all

// Map churn
var m = new Map();
for (var i = 0; i < 1000; i++) {
  m.set("key" + i, [i, i*2]);
}
m.size;
for (var i = 0; i < 1000; i += 2) {
  m.delete("key" + i);
}
m.size;

// Array churn with map/filter creating intermediate arrays
for (var i = 0; i < 200; i++) {
  var tmp = [i, i+1, i+2].map(x => x*2).filter(x => x > i);
}

// Nested object tree — deep then abandon
var tree = {};
var cur = tree;
for (var d = 0; d < 100; d++) {
  cur.child = {val: d, data: "node" + d};
  cur = cur.child;
}
tree = null; // GC should reclaim deep chain

// Rapid prototype instance creation
function Pt(x, y) { this.x = x; this.y = y; }
for (var i = 0; i < 1000; i++) {
  var p = new Pt(i, i*2);
}
