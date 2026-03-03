// Kostya Benchmark: json_gen (Node.js)
// JSON generation — build a large JSON structure and serialize
'use strict';

function jsonStr(s) {
    return '"' + s + '"';
}

function nextRand(seed) {
    return (seed * 1664525 + 1013904223) % 1000000;
}

function benchmark() {
    const numObjects = 1000;
    let seed = 42;
    const parts = ["["];
    for (let i = 0; i < numObjects; i++) {
        if (i > 0) parts.push(",");
        seed = nextRand(seed);
        const id = seed % 10000;
        seed = nextRand(seed);
        const x = Math.trunc(((seed % 20000) - 10000) / 100.0);
        seed = nextRand(seed);
        const y = Math.trunc(((seed % 20000) - 10000) / 100.0);
        seed = nextRand(seed);
        const score = seed % 100;

        const coord = "{" + jsonStr("x") + ":" + x + "," + jsonStr("y") + ":" + y + "}";
        const obj = "{" + jsonStr("id") + ":" + id + "," + jsonStr("score") + ":" + score + "," + jsonStr("coord") + ":" + coord + "," + jsonStr("active") + ":true}";
        parts.push(obj);
    }
    parts.push("]");
    const json = parts.join('');
    return json.length;
}

function main() {
    const __t0 = process.hrtime.bigint();
    let result = 0;
    for (let iter = 0; iter < 10; iter++) {
        result = benchmark();
    }
    const __t1 = process.hrtime.bigint();
    process.stdout.write("json_gen: length=" + result + "\n");
    if (result > 0) {
        process.stdout.write("json_gen: PASS\n");
    } else {
        process.stdout.write("json_gen: FAIL\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();
