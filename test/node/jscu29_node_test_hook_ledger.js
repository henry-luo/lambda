// JSCU29: node:test hook phases share growable rooted execution ledgers.
const { beforeEach, afterEach, test } = require("node:test");

let before_count = 0;
let after_count = 0;
for (let i = 0; i < 65; i++) {
    beforeEach(() => { before_count++; });
    afterEach(() => { after_count++; });
}

test("dynamic hook ledger", () => {
    console.log(before_count);
});
console.log(after_count);
