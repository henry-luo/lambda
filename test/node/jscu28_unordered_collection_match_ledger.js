// A Set stores distinct object identities. The final two left entries are
// structurally the same, while the right side has only one such entry.
const util = require('util');
const left = new Set();
const right = new Set();
for (let i = 0; i < 1024; i++) {
    left.add({ value: i });
    right.add({ value: i });
}
left.add({ value: 1024 });
left.add({ value: 1024 });
right.add({ value: 1024 });
right.add({ value: 1025 });
console.log(util.isDeepStrictEqual(left, right));
