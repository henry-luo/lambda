// Regression: vm.runInContext units sharing one context must each get their own
// module-var slot namespace. A constructor defined in unit A, invoked from unit B
// (with enough top-level declarations to collide per-unit slot indices), must resolve
// its own module-scoped name against unit A's slots. Previously the constructor read
// unit B's active slots and saw undefined — box2d's `new BenchmarkSuite(...)` failed
// with "Cannot destructure 'undefined'" (the .push receiver was undefined).
const vm = require('vm');
const ctx = { console };
vm.createContext(ctx);
vm.runInContext("function Suite(name){ Suite.all.push(this); this.name = name; } Suite.all = []; Suite.tag = 'S';", ctx, { filename: 'unitA' });
vm.runInContext(
  "function f0(){} function f1(){} function f2(){}" +
  "var s = new Suite('box2d');" +
  "console.log('tag=' + Suite.tag + ' count=' + Suite.all.length + ' name=' + s.name);",
  ctx, { filename: 'unitB' });
