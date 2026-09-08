let t = Date.now(); let s = 0;
for (let i = 0; i < 500000; i++) { const p = {x: i, y: 1}; s += p.x + p.y; }
console.log("{x,y} 500k:", Date.now() - t, "ms", s);
