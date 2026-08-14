function indexedGuards() {
    const typed = new Uint8Array(8);
    const dense = new Array(8);
    const exact = 1 | 0;
    dense.fill(1);
    typed[exact] = 3;
    dense[exact] = 4;
    typed[2] = 5;
    dense[2] = 6;
    return typed[exact] + dense[exact] + typed[2] + dense[2];
}

console.log(indexedGuards());
