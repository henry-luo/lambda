// JSCU29: console labels share one growable count/timer record.
console.count("label0");
for (let i = 1; i <= 64; i++) {
    console.countReset("label" + i);
}
console.count("label0");
