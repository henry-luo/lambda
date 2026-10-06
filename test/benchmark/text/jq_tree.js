// @benchmark-include jq/jq_helper.js
if (typeof runJqBenchmark === "undefined") { var runJqBenchmark = require("./jq/jq_helper.js").runJqBenchmark; }
// jq_tree (load: path updates over a 2^17-leaf tree) on jqjs + patches; the filter is jq/tree.jq
runJqBenchmark("jq_tree", "null", null, 313746104);
