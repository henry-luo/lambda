function tiny() { return 5e-324; }
function capture(value) { return () => value; }

let latest = 0;
for (let i = 0; i < 1000000; i++) latest = tiny();
for (let i = 0; i < 1000000; i++) tiny();

const first = tiny();
const second = -5e-324;
const indirect = [tiny][0];
const read = capture(tiny());
console.log(latest, first, second, indirect(), read(),
    Object.is(tiny() * 0, 0), Object.is(-tiny() * 0, -0));
