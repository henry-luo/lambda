// JSCU29: assert.throws expected-object patterns retain every key.
const assert = require("assert");

let expected = {};
let actual = {};
for (let i = 0; i < 129; i++) {
    expected["key" + i] = i;
    actual["key" + i] = i;
}

assert.throws(() => { throw actual; }, expected);
console.log("matched");

actual.key128 = -1;
try {
    assert.throws(() => { throw actual; }, expected);
    console.log("incorrectly accepted");
} catch (error) {
    console.log(error.name);
}
