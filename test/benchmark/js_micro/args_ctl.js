function f(o) { let s = 0; for (let k = 0; k < 3; k++) s += o.args; return s; }
const o = { args: 1 };
let t = Date.now(); let acc = 0;
for (let i = 0; i < 500000; i++) acc += f(o);
console.log("o.args:", Date.now() - t, "ms", acc);
