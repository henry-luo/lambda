// AWFY Benchmark: CD (Node.js) — uses official AWFY source
'use strict';
const { runAWFY } = require('./awfy_helper');
runAWFY('CD', require('../../../ref/are-we-fast-yet/benchmarks/JavaScript/cd'), 100);
