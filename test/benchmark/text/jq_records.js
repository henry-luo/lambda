// @benchmark-include jq/jq_helper.js
if (typeof runJqBenchmark === "undefined") { var runJqBenchmark = require("./jq/jq_helper.js").runJqBenchmark; }
// jq_records (breadth: order processing over orders.json) on jqjs + patches; the filter is jq/records.jq
runJqBenchmark("jq_records", "json", "test/benchmark/text/jq/orders.json", 878885883);
