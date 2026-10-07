// Shared driver for the jq_* text benchmarks on Node.js and LambdaJS.
// generate_jq_fixture.js appends this to the vendored jqjs (exports stripped)
// to form jq_helper.js; edit this file, then regenerate.
function runJqBenchmark(name, inputKind, inputPath, expected) {
    var fs = require("fs");
    var filter = fs.readFileSync("test/benchmark/text/jq/" + name.slice(3) + ".jq", "utf8");
    var input = null;
    if (inputKind === "json") input = JSON.parse(fs.readFileSync(inputPath, "utf8"));
    else if (inputKind === "raw") input = fs.readFileSync(inputPath, "utf8");
    // parsing the filter and loading the input stay outside the timed region
    var program = compile(filter);
    var started = performance.now();
    var outputs = [];
    for (var value of program(input)) outputs.push(value);
    var elapsed = performance.now() - started;
    if (outputs.length !== 1 || outputs[0] !== expected) {
        throw new Error(name + ": unexpected output " + JSON.stringify(outputs));
    }
    process.stdout.write(name + ": CHECKSUM:" + outputs[0] + "\n");
    process.stdout.write("__TIMING__:" + elapsed + "\n");
}
if (typeof module !== "undefined") module.exports = {runJqBenchmark: runJqBenchmark};
