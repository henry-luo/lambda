// AWFY Benchmark: NBody (Node.js) — uses official AWFY source
'use strict';
const { runAWFY } = require('./awfy_helper');
runAWFY('NBody', require('../../../ref/are-we-fast-yet/benchmarks/JavaScript/nbody'), 36000);
