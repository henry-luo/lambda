// Numeric Items in a dense array: append 20000 integers, then traverse 50 times.
'use strict';
function work() {
    let a = [];
    for (let i = 0; i < 20000; i++) a[i] = i;
    let sum = 0;
    for (let pass = 0; pass < 50; pass++) {
        for (let i = 0; i < a.length; i++) sum += a[i];
    }
    return sum;
}

function main() {
    const __t0 = performance.now();
    const result = work();
    const __t1 = performance.now();
    if (result === 9999500000) {
        process.stdout.write('integer_dense: PASS\n');
    } else {
        process.stdout.write('integer_dense: FAIL result=' + result + '\n');
    }
    process.stdout.write('__TIMING__:' + (__t1 - __t0) + '\n');
}
main();
