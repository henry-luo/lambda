const a = new Array(1000);
for (let i = 0; i < 1000; i++) a[i] = i;
let t = Date.now(); let s = 0;
for (let k = 0; k < 4000; k++) { for (let i = 0; i < 1000; i++) { a[i] = a[i] + 1; s += a[i]; } }
console.log("a[i] 4M rw:", Date.now() - t, "ms", s);
