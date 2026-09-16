// JSCU29: retain progress markers around node:test module initialization.
console.log("before node:test");
try {
    const node_test = require("node:test");
    console.log("after node:test", typeof node_test);
} catch (error) {
    console.log("node:test error", error.name, error.message);
}
