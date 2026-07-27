const perf = require('perf_hooks');
const nodePerf = require('node:perf_hooks');

console.log(perf === nodePerf, perf.default === perf);
console.log(perf.performance === globalThis.performance);
console.log(typeof perf.performance.now, typeof perf.performance.mark);
console.log(typeof perf.PerformanceObserver, typeof perf.PerformanceEntry);
console.log(typeof perf.monitorEventLoopDelay, typeof perf.createHistogram);
console.log(typeof perf.monitorEventLoopDelay(), typeof perf.createHistogram());
