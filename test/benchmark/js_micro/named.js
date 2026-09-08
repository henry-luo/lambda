const p = {x: 1, y: 2};
let t = Date.now(); let s = 0;
for (let i = 0; i < 4000000; i++) s += p.x + p.y;
console.log("p.x+p.y 4M:", Date.now() - t, "ms", s);
t = Date.now();
for (let i = 0; i < 4000000; i++) { p.x = i; p.y = i; }
console.log("p.x=,p.y= 4M:", Date.now() - t, "ms", p.x);
