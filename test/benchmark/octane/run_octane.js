// Octane benchmark runner for Node.js
// Runs individual Octane benchmarks using vm module to simulate V8 shell's load()
// Usage: node run_octane.js <benchmark_name>
// Example: node run_octane.js box2d
//          node run_octane.js all
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const dir = __dirname;

const BENCHMARKS = {
    'box2d':       ['box2d.js'],
    'code-load':   ['code-load.js'],
    'earley-boyer':['earley-boyer.js'],
    'pdfjs':       ['pdfjs.js'],
    'regexp':      ['regexp.js'],
    'typescript':  ['typescript-input.js', 'typescript-compiler.js', 'typescript.js'],
};

function runBenchmark(name, files) {
    // Only inject non-built-in globals. Let the sandbox use its own built-in
    // constructors (Array, Object, etc.) to avoid cross-realm instanceof failures.
    const ctx = {
        performance: { now: () => Date.now() },
        print: console.log,
        console,
    };
    vm.createContext(ctx);

    // Load harness
    vm.runInContext(fs.readFileSync(path.join(dir, 'base.js'), 'utf8'), ctx, { filename: 'base.js' });

    // Load benchmark files
    for (const f of files) {
        vm.runInContext(fs.readFileSync(path.join(dir, f), 'utf8'), ctx, { filename: f });
    }

    // Run
    vm.runInContext(`
        var success = true;
        function PrintResult(name, result) { print(name + ': ' + result); }
        function PrintError(name, error) { PrintResult(name, error); success = false; }
        function PrintScore(score) {
            if (success) { print('----'); print('Score (version ' + BenchmarkSuite.version + '): ' + score); }
        }
        BenchmarkSuite.config.doWarmup = undefined;
        BenchmarkSuite.config.doDeterministic = undefined;
        BenchmarkSuite.RunSuites({ NotifyResult: PrintResult, NotifyError: PrintError, NotifyScore: PrintScore });
    `, ctx);
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
