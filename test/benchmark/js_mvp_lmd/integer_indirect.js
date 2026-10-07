// Numeric Items through an indirect function's arguments and result.
'use strict';
function increment(x) { return x + 1; }
function work() {
    const callback = increment;
    let sum = 0;
    for (let i = 0; i < 200000; i++) sum += callback(i);
    return sum;
}

function main() {
    const __t0 = performance.now();
    const result = work();
    const __t1 = performance.now();
    if (result === 20000100000) {
        process.stdout.write('integer_indirect: PASS\n');
    } else {
        process.stdout.write('integer_indirect: FAIL result=' + result + '\n');
    }
    process.stdout.write('__TIMING__:' + (__t1 - __t0) + '\n');
}
main();
