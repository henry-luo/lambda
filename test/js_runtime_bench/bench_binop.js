// Measures arithmetic and bitwise binop cost. Used to gate any future folds
// of js_add / js_subtract / js_bitwise_and / js_left_shift / etc.

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

run("add_int", function (n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = s + i;
    return s;
}, ITERS);

run("sub_int", function (n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = s - i;
    return s;
}, ITERS);

run("mul_int", function (n) {
    let s = 1;
    for (let i = 0; i < n; i++) s = (s * 3) | 0;
    return s;
}, ITERS);

run("band_int", function (n) {
    let s = 0xffffff;
    for (let i = 0; i < n; i++) s = s & i;
    return s;
}, ITERS);

run("bor_int", function (n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = s | i;
    return s;
}, ITERS);

run("bxor_int", function (n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = s ^ i;
    return s;
}, ITERS);

run("lshift_int", function (n) {
    let s = 1;
    for (let i = 0; i < n; i++) s = (s << 1) | 0;
    return s;
}, ITERS);

run("rshift_int", function (n) {
    let s = 0x7fffffff;
    for (let i = 0; i < n; i++) s = s >> 1;
    return s;
}, ITERS);

run("urshift_int", function (n) {
    let s = 0xffffffff;
    for (let i = 0; i < n; i++) s = s >>> 1;
    return s;
}, ITERS);
