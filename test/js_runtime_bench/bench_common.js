// Tune8 §1.1 microbench common harness.
// Each microbench loads this then calls run(name, iters, fn).
// We deliberately do not depend on standard JS test frameworks — lambda.exe
// runs raw JS files and we want zero infra overhead so the measured cost is
// the call itself, not the harness.

const ITERS = 5_000_000;
const REPS = 5;

function run(name, fn, iters) {
    iters = iters || ITERS;
    // warmup so the JIT settles
    fn(iters);
    let best = Infinity;
    let sum = 0;
    for (let r = 0; r < REPS; r++) {
        const t0 = performance.now();
        fn(iters);
        const t1 = performance.now();
        const ms = t1 - t0;
        if (ms < best) best = ms;
        sum += ms;
    }
    const avg = sum / REPS;
    const ns_per_op_best = (best * 1e6) / iters;
    const ns_per_op_avg = (avg * 1e6) / iters;
    console.log(name + "\t" + iters + "\t" + best.toFixed(3) + "\t" + avg.toFixed(3)
        + "\t" + ns_per_op_best.toFixed(2) + "\t" + ns_per_op_avg.toFixed(2));
}

console.log("name\titers\tbest_ms\tavg_ms\tns_per_op_best\tns_per_op_avg");
