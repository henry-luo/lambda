// @benchmark-include jq/jq_helper.js
if (typeof runJqBenchmark === "undefined") { var runJqBenchmark = require("./jq/jq_helper.js").runJqBenchmark; }
// jq_bf (load: jaq's Brainfuck interpreter written in jq) on jqjs + patches; the filter is jq/bf.jq
runJqBenchmark("jq_bf", "raw", "test/benchmark/text/jq/fib.bf", 478890292);
