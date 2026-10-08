import expr: lambda.chart.expression

let row = {value: 12, name: "Alpha", nested: {count: 3}, present: null, values: [2, 4]};
let cases = [
    ["datum.value * 2 + 1", 25], ["2 + 3 * 4", 14], ["(2 + 3) * 4", 20],
    ["2 ** 3 ** 2", 512], ["10 % 3", 1], ["-2 + +3", 1],
    ["datum['nested'].count", 3], ["datum.values[1]", 4], ["datum.values.length", 2],
    ["datum.name.length", 5], ["datum.name[0]", "A"], ["datum['present']", null],
    ["datum.value >= 10 && datum.name === 'Alpha'", true], ["1 < 2 == true", true],
    ["'12' == 12", true], ["'12' === 12", false], ["false == 0", true], ["null == 0", false],
    ["datum.missing == null", true], ["datum.missing === null", false], ["datum.present === null", true],
    ["isDefined(datum.missing)", false], ["isDefined(datum.present)", true],
    ["isValid(NaN) || isValid(null)", false], ["isFinite(Infinity)", false],
    ["!0 && !'' && !null && !undefined", true], ["[] ? 1 : 0", 1], ["{} ? 1 : 0", 1],
    ["false && datum.missing.value", false], ["true || datum.missing.value", true],
    ["datum.value > 10 ? 'high' : datum.missing.value", "high"],
    ["if(false, datum.missing.value, 7)", 7], ["0 || 5", 5], ["2 && 3", 3],
    ["'item ' + datum.value", "item 12"], ["'a\\nb'", "a\nb"],
    ["'\\u4E2D\\uD83D\\uDE00'", "中😀"], [".5 + 1e2", 100.5],
    ["abs(-3) + pow(2, 3) + round(-1.5)", 10], ["clamp(20, 0, 10)", 10],
    ["floor(PI)", 3], ["min(3, 2, 1)", 1], ["max()", -inf],
    ["join(reverse([1, 2, 3]), '-')", "3-2-1"], ["extent([null, 2, NaN, 4])", [2, 4]],
    ["lower(trim(' AB '))", "ab"], ["substring('abc', 2, 0)", "ab"], ["slice('abcd', -3, -1)", "bc"],
    ["split('a,b,c', ',', 2)", ["a", "b"]], ["replace('a-a', 'a', 'x')", "x-a"],
    ["indexof([4, 5, 4], 4)", 0], ["lastindexof('banana', 'an')", 3], ["indexof('abc', 'z')", -1],
    ["inrange(3, [5, 1])", true], ["lerp([0, 10], .25)", 2.5],
    ["merge({a:1}, {'a':2, b:3})", {a: 2, b: 3}], ["pluck([{a:{b:2}}, {a:{b:3}}], 'a.b')", [2, 3]],
    ["format(12.5, '.1f')", "12.5"], ["1 << 31", -2147483648], ["-1 >>> 1", 2147483647],
    ["5 & 3 | 8", 9], ["~0", -1], ["5 ^ 3", 6]
];
let invalid = ["", "datum.", "datum[", "datum.value +", "'unterminated", "1e+", "1 = 2",
    "datum.value()", "unknown", "random()", "sqrt()", "if(true, 1)", "{a 1}", "[1 2]", "'\\uD800'"];
[
    for (test in cases where expr.evaluate(expr.compile(test[0]), row) != test[1]) {expression: test[0], failed: true},
    for (source in invalid where not (expr.compile(source) is error)) {invalid: source},
    for (check in [
        {label: "missing property", passed: expr.evaluate(expr.compile("datum.missing.value"), row) is error},
        {label: "predicate error", passed: expr.test(expr.compile("datum.missing.value"), row) is error},
        {label: "numeric coercion", passed: expr.evaluate(expr.compile("toNumber('bad')"), row) is nan}
    ] where not check.passed) check.label
]
