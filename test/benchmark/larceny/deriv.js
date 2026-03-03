// Larceny Benchmark: deriv (Node.js)
// Symbolic differentiation of expression trees
'use strict';

function deriv(e) {
    if (e.t === 0) return { t: 0, v: 0 };
    if (e.t === 1) return { t: 0, v: 1 };
    if (e.t === 2) {
        return { t: 2, l: deriv(e.l), r: deriv(e.r) };
    }
    // t === 3: product rule d(a*b) = a*db + da*b
    const dl = deriv(e.l);
    const dr = deriv(e.r);
    return { t: 2, l: { t: 3, l: e.l, r: dr }, r: { t: 3, l: dl, r: e.r } };
}

function countNodes(e) {
    if (e.t === 0 || e.t === 1) return 1;
    return 1 + countNodes(e.l) + countNodes(e.r);
}

function makeExpr() {
    const c3 = { t: 0, v: 3 };
    const c2 = { t: 0, v: 2 };
    const c5 = { t: 0, v: 5 };
    const x = () => ({ t: 1, v: 0 });
    const m1 = { t: 3, l: c3, r: x() };
    const m2 = { t: 3, l: m1, r: x() };
    const m3 = { t: 3, l: m2, r: x() };
    const m4 = { t: 3, l: c2, r: x() };
    const m5 = { t: 3, l: m4, r: x() };
    const a1 = { t: 2, l: m3, r: m5 };
    const a2 = { t: 2, l: a1, r: x() };
    return { t: 2, l: a2, r: c5 };
}

function benchmark() {
    let result = 0;
    for (let iter = 0; iter < 5000; iter++) {
        const e = makeExpr();
        const d = deriv(e);
        result = countNodes(d);
    }
    return result;
}

function main() {
    const __t0 = process.hrtime.bigint();
    const result = benchmark();
    const __t1 = process.hrtime.bigint();
    if (result === 45) {
        process.stdout.write("deriv: PASS\n");
    } else {
        process.stdout.write("deriv: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();
