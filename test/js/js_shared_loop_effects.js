// D5.3/D8.4.3v2: only total scalar leaves may move across loop control flow.
function pureLoop(n) {
    let total = 0;
    for (let i = 0; i < n; i++) total += Math.round(1.5) + Math.ceil(-0.5) + Math.pow(2, 3);
    return total;
}
console.log(pureLoop(0), pureLoop(8));
function liveLoop(n) {
    let total = 0;
    while (n > 0) { total += Math.round(n + 0.5); n--; }
    return total;
}
console.log(liveLoop(4));
let coercions = 0;
const object = {valueOf() { coercions++; return coercions + 0.5; }};
let result = 0;
for (let i = 0; i < 4; i++) result += Math.round(object);
console.log(result, coercions);
let calls = 0;
const saved = Math.round;
Math.round = x => { calls++; return saved(x); };
console.log(pureLoop(3), calls);
Math.round = saved;
function* suspended() {
    for (let i = 0; i < 3; i++) yield Math.round(i + 0.5);
}
console.log(Array.from(suspended()).join(','));
