// AWFY Benchmark: DeltaBlue (Node.js) — uses official AWFY source
'use strict';
const { runAWFY } = require('./awfy_helper');
runAWFY('DeltaBlue', require('../../../ref/are-we-fast-yet/benchmarks/JavaScript/deltablue'), 100, 20);
