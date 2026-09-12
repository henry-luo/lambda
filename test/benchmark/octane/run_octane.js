// Octane benchmark runner for Node.js
// Runs individual Octane benchmarks via indirect eval to simulate V8 shell's load()
// Usage: node run_octane.js <benchmark_name>
// Example: node run_octane.js box2d
//          node run_octane.js all
'use strict';
const fs = require('fs');
const path = require('path');
const dir = __dirname;

const BENCHMARKS = {
    'box2d':       ['box2d.js'],
    'code-load':   ['code-load.js'],
    'earley-boyer':['earley-boyer.js'],
    'pdfjs':       ['pdfjs.js'],
    'regexp':      ['regexp.js'],
    'typescript':  ['typescript-input.js', 'typescript-compiler.js', 'typescript.js'],
};

// Indirect eval runs at global scope, so each loaded file defines its globals
// on the shared realm exactly as V8's shell `load()` does. Unlike the former vm
// contexts this gives no per-benchmark isolation: run `all` only for a smoke
// pass and take real numbers one benchmark per process.
const load = (0, eval);

function runBenchmark(name, files) {
    // Only inject non-built-in globals. The benchmarks use the realm's own
    // built-in constructors (Array, Object, etc.), so instanceof stays valid.
    if (typeof globalThis.performance === 'undefined') {
        globalThis.performance = { now: () => Date.now() };
    }
    globalThis.print = console.log;

    // Load harness
    load(fs.readFileSync(path.join(dir, 'base.js'), 'utf8'));

    // Load benchmark files
    for (const f of files) {
        load(fs.readFileSync(path.join(dir, f), 'utf8'));
    }

    // Run
    load(`
        var success = true;
        function PrintResult(name, result) { print(name + ': ' + result); }
        function PrintError(name, error) { PrintResult(name, error); success = false; }
        function PrintScore(score) {
            if (success) { print('----'); print('Score (version ' + BenchmarkSuite.version + '): ' + score); }
        }
        BenchmarkSuite.config.doWarmup = undefined;
        BenchmarkSuite.config.doDeterministic = undefined;
        BenchmarkSuite.RunSuites({ NotifyResult: PrintResult, NotifyError: PrintError, NotifyScore: PrintScore });
    `);
}

const arg = process.argv[2] || 'all';
if (arg === 'all') {
    for (const [name, files] of Object.entries(BENCHMARKS)) {
        console.log(`\n=== ${name} ===`);
        try { runBenchmark(name, files); }
        catch (e) { console.log(`ERROR: ${e.message}`); }
    }
} else if (BENCHMARKS[arg]) {
    runBenchmark(arg, BENCHMARKS[arg]);
} else {
    console.log('Unknown benchmark: ' + arg);
    console.log('Available: ' + Object.keys(BENCHMARKS).join(', ') + ', all');
    process.exit(1);
}
