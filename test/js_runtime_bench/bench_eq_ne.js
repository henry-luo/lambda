// Measures the cost of === and !== under the Tune8 §2.1 inverse-pair fold.
// Pre-fold:  === emits js_eq_raw call; !== emits js_ne_raw call (separate runtime fn)
// Post-fold: === emits js_eq_raw call; !== emits js_eq_raw call + MIR_XOR with 1
//
// Hypothesis: !== should be ~1ns slower than === (one extra XOR insn). If the
// delta is larger, the fold has a real cost; if it's noise, the fold is free.

// Inline the harness so this file is standalone.
const ITERS = 5_000_000;
const REPS = 5;
console.log("name\titers\tbest_ms\tavg_ms\tns_per_op_best\tns_per_op_avg");

function run(name, fn, iters) {
    fn(iters);  // warmup
    let best = Infinity, sum = 0;
    for (let r = 0; r < REPS; r++) {
        const t0 = performance.now();
        fn(iters);
        const t1 = performance.now();
        const ms = t1 - t0;
        if (ms < best) best = ms;
        sum += ms;
    }
    const avg = sum / REPS;
    console.log(name + "\t" + iters + "\t" + best.toFixed(3) + "\t" + avg.toFixed(3)
        + "\t" + ((best * 1e6) / iters).toFixed(2)
        + "\t" + ((avg  * 1e6) / iters).toFixed(2));
}

// Two operands that vary so the comparison can't be constant-folded.
const A = [1, 2, 3, 4, 5, 6, 7, 8];
const B = [2, 2, 2, 2, 2, 2, 2, 2];

run("strict_eq", function (n) {
    let c = 0;
    for (let i = 0; i < n; i++) {
        if (A[i & 7] === B[i & 7]) c++;
    }
    return c;
}, ITERS);

run("strict_ne", function (n) {
    let c = 0;
    for (let i = 0; i < n; i++) {
        if (A[i & 7] !== B[i & 7]) c++;
    }
    return c;
}, ITERS);

run("loose_eq", function (n) {
    let c = 0;
    for (let i = 0; i < n; i++) {
        if (A[i & 7] == B[i & 7]) c++;
    }
    return c;
}, ITERS);

run("loose_ne", function (n) {
    let c = 0;
    for (let i = 0; i < n; i++) {
        if (A[i & 7] != B[i & 7]) c++;
    }
    return c;
}, ITERS);
