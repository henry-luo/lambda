#!/usr/bin/env node
// Node.js JetStream benchmark runner — measures same workloads as Lambda versions
// Usage: node test/benchmark/jetstream/run_jetstream_node.js [num_runs]

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const NUM_RUNS = parseInt(process.argv[2]) || 3;
const ROOT = path.resolve(__dirname, '../../..');

const benchmarks = [
    { name: 'nbody',        file: 'test/benchmark/jetstream/n-body.js' },
    { name: 'cube3d',       file: 'test/benchmark/jetstream/3d-cube.js' },
    { name: 'navier_stokes',file: 'test/benchmark/jetstream/navier-stokes.js' },
    { name: 'richards',     file: 'test/benchmark/jetstream/richards.js' },
    { name: 'splay',        file: 'test/benchmark/jetstream/splay.js' },
    { name: 'deltablue',    file: 'test/benchmark/jetstream/deltablue.js' },
    { name: 'hashmap',      file: 'test/benchmark/jetstream/hash-map.js' },
    { name: 'crypto_sha1',  file: 'test/benchmark/jetstream/crypto-sha1.js' },
    { name: 'raytrace3d',   file: 'test/benchmark/jetstream/3d-raytrace.js' },
];

function median(arr) {
    const s = arr.slice().sort((a, b) => a - b);
    const n = s.length;
    if (n === 0) return null;
    if (n % 2 === 1) return s[Math.floor(n / 2)];
    return (s[Math.floor(n / 2) - 1] + s[Math.floor(n / 2)]) / 2;
}

function runBenchmark(b) {
    // Create a wrapper that sources the benchmark file and calls runIteration with timing
    const jsPath = path.join(ROOT, b.file);
    const wrapper = `
${fs.readFileSync(jsPath, 'utf-8')}
const b = new Benchmark();
const t0 = performance.now();
b.runIteration();
const t1 = performance.now();
console.log("__TIMING__:" + (t1 - t0).toFixed(3));
`;
    const tmpFile = path.join(ROOT, 'temp', `_node_bench_${b.name}.js`);
    fs.writeFileSync(tmpFile, wrapper);

    const times = [];
    let ok = true;
    for (let i = 0; i < NUM_RUNS; i++) {
        try {
            const out = execSync(`node "${tmpFile}"`, {
                timeout: 120000,
                encoding: 'utf-8',
                cwd: ROOT,
            });
            const m = out.match(/__TIMING__:([\d.]+)/);
            if (m) {
                times.push(parseFloat(m[1]));
            } else {
                ok = false;
            }
        } catch (e) {
            ok = false;
        }
    }

    // Clean up
    try { fs.unlinkSync(tmpFile); } catch(_) {}

    return { ok, median: ok && times.length ? median(times) : null };
}

console.log(`JetStream Node.js Benchmarks — ${NUM_RUNS} run(s) each`);
console.log(`${'Benchmark'.padEnd(20)} ${'Exec (ms)'.padStart(12)} ${'Status'.padEnd(10)}`);
console.log('-'.repeat(45));

const results = [];
for (const b of benchmarks) {
    const r = runBenchmark(b);
    const ms = r.median !== null ? r.median.toFixed(1) : '—';
    const status = r.ok ? 'OK' : 'FAIL';
    console.log(`${b.name.padEnd(20)} ${ms.padStart(12)} ${status.padEnd(10)}`);
    results.push({ name: b.name, exec_ms: r.median, status });
}

console.log('-'.repeat(45));
const passed = results.filter(r => r.status !== 'FAIL').length;
console.log(`Passed: ${passed}/${results.length}`);

const execTimes = results.filter(r => r.exec_ms > 0).map(r => r.exec_ms);
if (execTimes.length) {
    const geoMean = Math.exp(execTimes.reduce((s, t) => s + Math.log(t), 0) / execTimes.length);
    console.log(`Geometric mean (exec): ${geoMean.toFixed(1)} ms`);
}
