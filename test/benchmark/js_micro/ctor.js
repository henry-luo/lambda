function P(x, y) { this.x = x; this.y = y; }
let t = Date.now(); let s = 0;
for (let i = 0; i < 500000; i++) { const p = new P(i, 1); s += p.x + p.y; }
console.log("new P 500k:", Date.now() - t, "ms", s);
