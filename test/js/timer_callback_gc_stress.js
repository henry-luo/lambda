// Ledger §15.1 / JO12: the event-loop drain once caught SIGSEGV to survive
// "heap corruption in timer callbacks". That guard is gone and a fault now
// fail-stops, so this drives callbacks through allocation, nested scheduling,
// clears from inside callbacks and interleaved promise jobs; the forced-GC
// lane runs it with freed memory poisoned. The summary prints when the last
// callback finishes, not on a clock, so a slow forced-GC run cannot reorder it.
let total = 0;
let pending = 0;
const log = [];

function churn(seed) {
    const items = [];
    for (let i = 0; i < 12; i++) {
        items.push({ id: seed * 100 + i, name: "n" + seed + "_" + i, tags: [i, i + 1] });
    }
    const bias = (x) => x + items.length;
    return items.reduce((acc, it) => acc + it.tags[1] + it.name.length, bias(0));
}

function done() {
    if (--pending > 0) return;
    console.log(total);
    console.log(log.sort().join(","));
}

for (let t = 0; t < 16; t++) {
    const captured = { t, text: "timer-" + t };
    pending++;
    setTimeout((extra, pair) => {
        total += churn(t) + extra + pair.length;
        if (t % 5 === 0) {
            pending++;
            setTimeout(() => {
                total += churn(t + 1000);
                log.push("nested-" + captured.text);
                done();
            }, 0);
        }
        pending++;
        Promise.resolve(captured).then((c) => {
            total += c.t;
            done();
        });
        done();
    }, t % 3, t, [t, t + 1]);
}

const cancelled = setTimeout(() => log.push("cancelled timer fired"), 5);
setTimeout(() => clearTimeout(cancelled), 0);
