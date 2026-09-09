// JSCU31: fake scheduler waits are dynamic records, not a 128-entry table.
const { mock } = require("node:test");
const timers = require("node:timers/promises");

mock.timers.enable();
let completed = 0;
for (let i = 0; i < 129; i++) {
    timers.scheduler.wait(1).then(() => { completed++; });
}
mock.timers.tick(1);
console.log(completed);
mock.timers.reset();
