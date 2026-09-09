// JSCU31: timer handles use the shared dynamic resource table, not a cap.
let fired = 0;
for (let i = 0; i < 1025; i++) {
    setTimeout(() => { fired++; }, 0);
}
setTimeout(() => console.log(fired), 2);

let interval_fired = 0;
let interval = setInterval(() => { interval_fired++; }, 0);
clearInterval(interval);
setTimeout(() => console.log(interval_fired), 2);
