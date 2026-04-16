// Stress: type confusion — rapidly change types on same variable
// Tests inline cache invalidation and type dispatch

var v;

// Cycle through all types
v = 42;          typeof v;
v = 3.14;        typeof v;
v = "hello";     typeof v;
v = true;        typeof v;
v = null;        typeof v;
v = undefined;   typeof v;
v = [1, 2, 3];   typeof v;
v = {x: 1};      typeof v;
v = function() { return 1; }; typeof v;
v = /regex/;     typeof v;

// Use after each type change
v = 42;        try { v + 1; } catch(e) {}
v = "hello";   try { v + 1; } catch(e) {}
v = null;      try { v + 1; } catch(e) {}
v = [1,2];     try { v + 1; } catch(e) {}
v = {x:1};     try { v + 1; } catch(e) {}

// Property access on changing types
v = {x: 1};   try { v.x; } catch(e) {}
v = [1,2,3];   try { v.length; } catch(e) {}
v = "abc";     try { v.length; } catch(e) {}
v = 42;        try { v.length; } catch(e) {}
v = null;      try { v.length; } catch(e) {}

// Method calls on changing types
v = [1,2,3];   try { v.push(4); } catch(e) {}
v = "abc";     try { v.charAt(0); } catch(e) {}
v = {m() { return 1; }}; try { v.m(); } catch(e) {}
v = 42;        try { v.toFixed(2); } catch(e) {}

// Polymorphic function — called with many different types
function poly(x) {
  try { return x + 1; } catch(e) { return null; }
}
poly(1);
poly(3.14);
poly("str");
poly(null);
poly(undefined);
poly(true);
poly([1]);
poly({valueOf() { return 10; }});

// Polymorphic property access
function getProp(obj) {
  try { return obj.x; } catch(e) { return undefined; }
}
getProp({x: 1});
getProp({x: "str", y: 2});
getProp({x: [1], y: 2, z: 3});
getProp({a: 1}); // no x property
getProp(null);
getProp(42);

// Megamorphic: many different shapes at same site
function readKey(o) { return o.key; }
for (var i = 0; i < 30; i++) {
  var obj = {key: i};
  for (var j = 0; j < i; j++) { obj["extra" + j] = j; }
  readKey(obj);
}
