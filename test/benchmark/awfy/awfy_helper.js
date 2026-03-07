// AWFY Benchmark Runner Helper
// Wraps official AWFY benchmark modules with __TIMING__ output
// Usage: const { runAWFY } = require('./awfy_helper');
//        runAWFY('Sieve', require('...path.../sieve'), 1);
'use strict';

function runAWFY(name, mod, innerIterations) {
    if (innerIterations === undefined) innerIterations = 1;
    const bench = mod.newInstance();
    const __t0 = process.hrtime.bigint();
    const ok = bench.innerBenchmarkLoop(innerIterations);
    const __t1 = process.hrtime.bigint();

    if (ok) {
        process.stdout.write(name + ": PASS\n");
    } else {
        process.stdout.write(name + ": FAIL\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

exports.runAWFY = runAWFY;
