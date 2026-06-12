// Measures property set/get cost — the hot path that the proposal §2.2
// load/store dispatcher fold *might* affect. Used to guard any future fold of
// js_property_set / js_property_get / js_property_set_strict.

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

// Stable-shape object with hot-known property.
function makeObj() { return { x: 0 }; }

run("prop_set_own", function (n) {
    const o = makeObj();
    for (let i = 0; i < n; i++) {
        o.x = i;
    }
    return o.x;
}, ITERS);

run("prop_get_own", function (n) {
    const o = makeObj();
    o.x = 42;
    let s = 0;
    for (let i = 0; i < n; i++) {
        s += o.x;
    }
    return s;
}, ITERS);

(function strict_mode() {
    "use strict";
    run("prop_set_own_strict", function (n) {
        const o = makeObj();
        for (let i = 0; i < n; i++) {
            o.x = i;
        }
        return o.x;
    }, ITERS);

    run("prop_get_own_strict", function (n) {
        const o = makeObj();
        o.x = 42;
        let s = 0;
        for (let i = 0; i < n; i++) {
            s += o.x;
        }
        return s;
    }, ITERS);
})();
