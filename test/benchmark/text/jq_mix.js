// @benchmark-include jq/jq_helper.js
if (typeof runJqBenchmark === "undefined") { var runJqBenchmark = require("./jq/jq_helper.js").runJqBenchmark; }
// jq_mix (breadth: jaq's bench filters) on jqjs + patches; the filter is jq/mix.jq
runJqBenchmark("jq_mix", "null", null, 98172625);
