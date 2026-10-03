// Shared scalar workloads. All counts, loop orders and oracles are in SUITE.md.
function decimalText(value) {
    var negative = value < 0;
    value = Math.abs(value);
    var divisor = 1;
    while (Math.floor(value / divisor) >= 10) divisor *= 10;
    var text = negative ? "-" : "";
    while (divisor > 0) {
        text += String.fromCharCode(48 + Math.floor(value / divisor));
        value %= divisor;
        divisor = Math.floor(divisor / 10);
    }
    return text;
}
function decimalValue(text) {
    var negative = text.charAt(0) === "-", index = negative ? 1 : 0, value = 0;
    while (index < text.length) value = value * 10 + text.charCodeAt(index++) - 48;
    return negative ? -value : value;
}
function zeros(count) {
    var result = new Array(count);
    for (var i = 0; i < count; i++) result[i] = 0.0;
    return result;
}
function parseIntegers() {
    var seed = 42, checksum = 0, size = 0, errors = 0;
    for (var index = 0; index < 100000; index++) {
        seed = seed * 16807 % 2147483647;
        var value = index % 8 === 0 ? 0 : (index % 8 === 1 ? -seed : seed);
        var text = decimalText(value), parsed = decimalValue(text);
        if (parsed !== value) errors++;
        size += text.length;
        checksum = (checksum * 31 + parsed + 2147483647) % 1000000007;
    }
    return [checksum, size, seed, errors];
}
function gram(matrix, rows, columns) {
    var result = zeros(columns * columns);
    for (var i = 0; i < columns; i++) for (var j = 0; j < columns; j++) {
        var total = 0.0;
        for (var k = 0; k < rows; k++) total += matrix[k * columns + i] * matrix[k * columns + j];
        result[i * columns + j] = total;
    }
    return result;
}
function square(matrix, n) {
    var result = zeros(n * n);
    for (var i = 0; i < n; i++) for (var j = 0; j < n; j++) {
        var total = 0.0;
        for (var k = 0; k < n; k++) total += matrix[i * n + k] * matrix[k * n + j];
        result[i * n + j] = total;
    }
    return result;
}
function traceFourth(matrix, rows, columns) {
    var fourth = square(square(gram(matrix, rows, columns), columns), columns), total = 0.0;
    for (var i = 0; i < columns; i++) total += fourth[i * columns + i];
    return total;
}
function variation(values) {
    var total = 0.0;
    for (var i = 0; i < values.length; i++) total += values[i];
    var mean = total / values.length;
    total = 0.0;
    for (var j = 0; j < values.length; j++) {
        var delta = values[j] - mean;
        total += delta * delta;
    }
    return Math.sqrt(total / (values.length - 1)) / mean;
}
function matrixStatistics() {
    var seed = 42, digest = 0, v = zeros(1000), w = zeros(1000);
    for (var iteration = 0; iteration < 1000; iteration++) {
        var blocks = zeros(100), p = zeros(100), q = zeros(100);
        for (var i = 0; i < 100; i++) {
            seed = seed * 16807 % 2147483647;
            blocks[i] = seed / 2147483647.0 * 2.0 - 1.0;
        }
        for (var block = 0; block < 4; block++) for (var row = 0; row < 5; row++) for (var column = 0; column < 5; column++) {
            var value = blocks[block * 25 + row * 5 + column];
            p[row * 20 + block * 5 + column] = value;
            q[(Math.floor(block / 2) * 5 + row) * 10 + block % 2 * 5 + column] = value;
        }
        v[iteration] = traceFourth(p, 5, 20);
        w[iteration] = traceFourth(q, 10, 10);
        digest = (digest * 31 + Math.floor(v[iteration] * 1000)) % 1000000007;
        digest = (digest * 31 + Math.floor(w[iteration] * 1000)) % 1000000007;
    }
    return [Math.floor(variation(v) * 1e9), Math.floor(variation(w) * 1e9), digest, seed];
}
function iterationPiSum() {
    var values = zeros(500);
    for (var iteration = 0; iteration < 500; iteration++) {
        var total = 0.0;
        for (var k = 1; k <= 10000 + iteration; k++) total += 1.0 / (k * k);
        values[iteration] = total;
    }
    var digest = 0.0;
    for (var i = 0; i < 500; i++) digest += values[i] * (i + 1);
    return [Math.floor(values[0] * 1e12), Math.floor(values[499] * 1e12), Math.floor(digest * 1e6), 5124750];
}
function formattedOutput() {
    var fs = require("fs"), sink = process.platform === "win32" ? "NUL" : "/dev/null";
    var size = 0, digest = 0, writes = 0, buffer = "";
    for (var i = 1; i <= 100000; i++) {
        var line = decimalText(i) + " " + decimalText(i + 1) + "\n";
        for (var j = 0; j < line.length; j++) digest = (digest * 31 + line.charCodeAt(j)) % 1000000007;
        size += line.length;
        buffer += line;
        if (i % 256 === 0 || i === 100000) {
            fs.writeFileSync(sink, buffer);
            writes++;
            buffer = "";
        }
    }
    return [size, digest, writes, 100000];
}
function microWorkload(name) {
    if (name === "parse_integers") return parseIntegers();
    if (name === "matrix_statistics") return matrixStatistics();
    if (name === "iteration_pi_sum") return iterationPiSum();
    if (name === "formatted_output") return formattedOutput();
    throw new Error("unknown Julia microbenchmark: " + name);
}
function microVerify(name, result) {
    var expected;
    if (name === "parse_integers") expected = [592470661, 854479, 1966931148, 0];
    if (name === "matrix_statistics") expected = [464726438, 486656926, 47509838, 1966931148];
    if (name === "iteration_pi_sum") expected = [1644834071848, 1644838824217, 206015869118, 5124750];
    if (name === "formatted_output") expected = [1177795, 584298900, 391, 100000];
    for (var i = 0; i < 4; i++) if (result[i] !== expected[i]) throw new Error(name + ": FAIL " + result);
}
function runJuliaMicro(name) {
    microVerify(name, microWorkload(name));
    var started = performance.now(), result = microWorkload(name), elapsed = performance.now() - started;
    microVerify(name, result);
    process.stdout.write(name + ": PASS " + result[0] + " " + result[1] + " " + result[2] + " " + result[3] + "\n");
    process.stdout.write("__TIMING__:" + elapsed + "\n");
}
if (typeof module !== "undefined") module.exports = {runJuliaMicro: runJuliaMicro};
