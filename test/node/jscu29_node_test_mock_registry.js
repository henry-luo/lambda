// JSCU29: node:test mock records use one growable payload-addressed registry.
const { mock } = require("node:test");

let invoked = 0;
let last = null;
for (let i = 0; i < 65; i++) {
    last = mock.fn(() => {
        invoked++;
        return i;
    });
}

console.log(last(), last.mock.callCount());
console.log(invoked);
