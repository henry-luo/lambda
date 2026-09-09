const functions = [];
for (let i = 0; i < 257; i++) {
    functions.push(new Function('return ' + i + ';'));
}

let total = 0;
for (let i = 0; i < functions.length; i++) {
    total += functions[i]();
}
gc();
console.log(total);
console.log(functions[256]());
