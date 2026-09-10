// D5.3/D8.4.3v2: a constant ToInt32 may move; a changing input must stay live.
function constantBits(n) {
    let total = 0;
    for (let i = 0; i < n; i++) total += (1.5 | i);
    return total;
}
function varyingBits(n) {
    let total = 0;
    for (let i = 0; i < n; i++) total += ((i + 0.5) | i);
    return total;
}
console.log(constantBits(0), constantBits(4), varyingBits(4));
