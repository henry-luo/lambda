// AWFY Benchmark: Mandelbrot (Node.js) — uses official AWFY source
'use strict';
const { runAWFY } = require('./awfy_helper');
runAWFY('Mandelbrot', require('../../../ref/are-we-fast-yet/benchmarks/JavaScript/mandelbrot'), 500);
