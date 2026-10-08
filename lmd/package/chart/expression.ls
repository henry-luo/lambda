// Static Vega expressions: parse once, then evaluate with only the current datum.
// The adapter uses JavaScript truthiness/coercion without changing Lambda's S3/S5 rules.
import numbers: .numbers
import util: .util

let constants = {'true': true, 'false': false, 'null': null, NaN: nan, Infinity: inf,
    PI: math.pi, E: math.e, LN2: math.log(2.0), LN10: math.log(10.0),
    LOG2E: 1.0 / math.log(2.0), LOG10E: 1.0 / math.log(10.0),
    SQRT1_2: sqrt(0.5), SQRT2: sqrt(2.0), MAX_VALUE: 1.7976931348623157e308, MIN_VALUE: 5e-324}
let numeric_functions = {abs: (x) => abs(x), acos: (x) => math.acos(x), asin: (x) => math.asin(x),
    atan: (x) => math.atan(x), ceil: (x) => ceil(x), cos: (x) => math.cos(x), exp: (x) => math.exp(x),
    floor: (x) => floor(x), log: (x) => math.log(x), round: (x) => floor(x + 0.5),
    sin: (x) => math.sin(x), sqrt: (x) => sqrt(x), tan: (x) => math.tan(x)}
let value_functions = {length: (value) => len(value), lower: (value) => lower(value),
    upper: (value) => upper(value), trim: (value) => trim(value),
    peek: (value) => value[len(value) - 1], reverse: (value) => reverse(value),
    sort: (value) => sort(value), span: (value) => value[len(value) - 1] - value[0]}
let arities = {
    *:map([for (registry in [numeric_functions, value_functions])
        for (key, handler in registry) for (item in [string(key), [1, 1]]) item]),
    isArray: [1,1], isBoolean: [1,1], isDate: [1,1], isDefined: [1,1], isNumber: [1,1],
    isObject: [1,1], isString: [1,1], isValid: [1,1], isNaN: [1,1], isFinite: [1,1],
    toBoolean: [1,1], toNumber: [1,1], toString: [1,1], 'if': [3,3],
    atan2: [2,2], clamp: [3,3], hypot: [0,null], max: [0,null], min: [0,null], pow: [2,2],
    indexof: [2,2], lastindexof: [2,2],
    slice: [2,3], substring: [2,3], split: [2,3], join: [1,2], replace: [3,3],
    inrange: [2,2], extent: [1,1],
    lerp: [2,2], pluck: [2,2], merge: [0,null], format: [2,2]
}
let precedence = {'||': 1, '&&': 2, '|': 3, '^': 4, '&': 5,
    '==': 6, '!=': 6, '===': 6, '!==': 6, '<': 7, '<=': 7, '>': 7, '>=': 7,
    '<<': 8, '>>': 8, '>>>': 8, '+': 9, '-': 9, '*': 10, '/': 10, '%': 10, '**': 11}

fn failure(message, position) error => error("chart: expression " ++ message ++ " at " ++ string(position))
fn character(source, index) => slice(source, index, index + 1)
fn digit(ch) => ch != "" and ch >= "0" and ch <= "9"
fn identifier(ch) => ch != "" and ((ch >= "a" and ch <= "z") or
    (ch >= "A" and ch <= "Z") or ch == "_" or ch == "$")
fn scan(source, index, numeric = false) {
    let ch = character(source, index);
    if (if (numeric) digit(ch) else identifier(ch) or digit(ch)) scan(source, index + 1, numeric) else index
}
fn hex_value(ch) => index_of("0123456789abcdef", lower(ch))
fn hex_number(source, begin, end) {
    let digits = [for (i in begin to (end - 1)) hex_value(character(source, i))];
    if (end > len(source) or len(digits) == 0 or contains(digits, null)) failure("invalid hexadecimal escape", begin)
    else sum([for (i, value in digits) value * (16.0 ** float(len(digits) - i - 1))])
}
fn string_token(source, index, quote, parts) {
    let ch = character(source, index);
    if (ch == "") failure("unterminated string", index)
    else if (ch == quote) {kind: "literal", value: join(parts, ""), end: index + 1}
    else if (ch == "\n" or ch == "\r") failure("newline in string", index)
    else if (ch != "\\") string_token(source, index + 1, quote, [*parts, ch])
    else {
        let escaped = character(source, index + 1);
        let escapes = {'n': "\n", 'r': "\r", 't': "\t", 'b': "\b", 'f': "\f", 'v': chr(11), '0': chr(0)};
        if (escaped == "") failure("unterminated escape", index)
        else if (escaped == "u" or escaped == "x") {
            let end = index + (if (escaped == "u") 6 else 4);
            let code = hex_number(source, index + 2, end);
            let paired = code is number and code >= 55296 and code <= 56319 and slice(source, end, end + 2) == "\\u";
            let low = if (paired) hex_number(source, end + 2, end + 6) else null;
            if (code is error) code
            else if (paired and (not (low is number) or low < 56320 or low > 57343)) failure("invalid surrogate pair", end)
            else if (not paired and code >= 55296 and code <= 57343) failure("unpaired surrogate", index)
            else string_token(source, if (paired) end + 6 else end, quote,
                [*parts, chr(int(if (paired) 65536 + (code - 55296) * 1024 + low - 56320 else code))])
        } else string_token(source, index + 2, quote,
            [*parts, if (escapes[escaped] != null) escapes[escaped] else escaped])
    }
}
fn number_token(source, index) {
    let whole_end = scan(source, index, true);
    let fraction_end = if (character(source, whole_end) == ".") scan(source, whole_end + 1, true) else whole_end;
    let exponent = lower(character(source, fraction_end)) == "e";
    let exponent_begin = fraction_end + 1;
    let exponent_digits = if (contains(["+", "-"], character(source, exponent_begin))) exponent_begin + 1 else exponent_begin;
    let end = if (exponent) scan(source, exponent_digits, true) else fraction_end;
    let raw = slice(source, index, end);
    let value = float(raw);
    if (value is error or (exponent and end == exponent_digits)) failure("invalid number", index)
    else {kind: "literal", value: value, end: end}
}
fn next_token(source, index) {
    let ch = character(source, index);
    if (ch == "\"" or ch == "'") string_token(source, index + 1, ch, [])
    else if (digit(ch) or (ch == "." and digit(character(source, index + 1)))) number_token(source, index)
    else if (identifier(ch)) {
        let end = scan(source, index + 1);
        {kind: "identifier", value: slice(source, index, end), end: end}
    } else {
        let operators = [for (width in [3, 2, 1], let op = slice(source, index, index + width)
            where len(op) == width and (precedence[op] != null or contains(["!", "~", "?", ":", ".", ",", "(", ")", "[", "]", "{", "}"], op))) op];
        if (len(operators) == 0) failure("unexpected character " ++ ch, index)
        else {kind: "operator", value: operators[0], end: index + len(operators[0])}
    }
}
fn tokenize(source, index = 0, tokens = []) {
    let ch = character(source, index);
    if (index >= len(source)) tokens
    else if (contains([" ", "\t", "\n", "\r"], ch)) tokenize(source, index + 1, tokens)
    else {
        let token = next_token(source, index);
        if (token is error) token else tokenize(source, token.end, [*tokens, {*:token, position: index}])
    }
}
fn literal(value) => {kind: "literal", value: value}
fn parsed(node, index) => {node: node, index: index}
fn expect(tokens, index, text) => if (tokens[index].value == text) index + 1
    else failure("expected " ++ text, if (tokens[index] != null) tokens[index].position else -1)

fn parse_list(tokens, index, close, items = [], object_value = false) {
    if (tokens[index].value == close) {items: items, index: index + 1}
    else {
        let key = tokens[index];
        let begin = if (object_value) expect(tokens, index + 1, ":") else index;
        let item = if (begin is error) begin else parse_expression(tokens, begin);
        if (object_value and not (key.kind == "identifier" or key.kind == "literal")) failure("invalid object key", key.position)
        else if (item is error) item
        else {
            let next = tokens[item.index].value;
            let entries = [*items, if (object_value) {key: string(key.value), node: item.node} else item.node];
            if (next == close) {items: entries, index: item.index + 1}
            else if (next == ",") parse_list(tokens, item.index + 1, close, entries, object_value)
            else failure("expected comma or " ++ close, item.index)
        }
    }
}
fn parse_primary(tokens, index) {
    let token = tokens[index];
    let value = token.value;
    if (token == null) failure("expected operand", index)
    else if (token.kind == "literal") parsed(literal(value), index + 1)
    else if (value == "(") {
        let inner = parse_expression(tokens, index + 1);
        let end = if (inner is error) inner else expect(tokens, inner.index, ")");
        if (end is error) end else parsed(inner.node, end)
    } else if (value == "[" or value == "{") {
        let entries = parse_list(tokens, index + 1, if (value == "[") "]" else "}", [], value == "{");
        if (entries is error) entries else parsed({kind: if (value == "[") "array" else "object_value", items: entries.items}, entries.index)
    } else if (contains(["!", "~", "+", "-"], value)) {
        let operand = parse_unary(tokens, index + 1);
        if (operand is error) operand else parsed({kind: "unary", op: value, arg: operand.node}, operand.index)
    } else if (token.kind != "identifier") failure("unexpected token " ++ string(value), token.position)
    else if (tokens[index + 1].value == "(") {
        let args = parse_list(tokens, index + 2, ")");
        let arity = arities[value];
        if (arity == null) failure("unknown function " ++ value, token.position)
        else if (args is error) args
        else if (len(args.items) < arity[0] or (arity[1] != null and len(args.items) > arity[1])) failure("invalid argument count for " ++ value, token.position)
        else parsed({kind: "call", function_name: value, args: args.items}, args.index)
    } else if (value == "datum") parsed({kind: "datum"}, index + 1)
    else if (value == "undefined") parsed({kind: "undefined"}, index + 1)
    else if (contains([for (key, item in constants) string(key)], value)) parsed(literal(constants[value]), index + 1)
    else failure("unknown variable " ++ value, token.position)
}
fn parse_postfix(tokens, value) {
    if (value is error) value
    else if (tokens[value.index].value == ".") {
        let key = tokens[value.index + 1];
        if (key.kind != "identifier") failure("expected property name", value.index + 1)
        else parse_postfix(tokens, parsed({kind: "member", object_value: value.node, key: literal(key.value)}, value.index + 2))
    } else if (tokens[value.index].value == "[") {
        let key = parse_expression(tokens, value.index + 1);
        let end = if (key is error) key else expect(tokens, key.index, "]");
        if (end is error) end else parse_postfix(tokens, parsed({kind: "member", object_value: value.node, key: key.node}, end))
    } else value
}
fn parse_unary(tokens, index) => parse_postfix(tokens, parse_primary(tokens, index))
fn parse_binary(tokens, left, minimum) {
    if (left is error) left else {
        let op = tokens[left.index].value;
        let rank = precedence[op];
        if (rank == null or rank < minimum) left
        else {
            let right = parse_binary(tokens, parse_unary(tokens, left.index + 1), rank + (if (op == "**") 0 else 1));
            if (right is error) right else parse_binary(tokens,
                parsed({kind: "binary", op: op, left: left.node, right: right.node}, right.index), minimum)
        }
    }
}
fn parse_expression(tokens, index) {
    let test = parse_binary(tokens, parse_unary(tokens, index), 1);
    if (test is error or tokens[test.index].value != "?") test else {
        let yes = parse_expression(tokens, test.index + 1);
        let separator = if (yes is error) yes else expect(tokens, yes.index, ":");
        let no = if (separator is error) separator else parse_expression(tokens, separator);
        if (no is error) no else parsed({kind: "conditional", test: test.node, yes: yes.node, no: no.node}, no.index)
    }
}

pub fn compile(source) {
    if (not (source is string)) failure("must be a string", 0) else {
        let tokens = tokenize(source);
        let parsed = if (tokens is error) tokens else parse_expression(tokens, 0);
        if (parsed is error) parsed
        else if (parsed.index != len(tokens)) failure("unexpected token " ++ string(tokens[parsed.index].value), tokens[parsed.index].position)
        else parsed.node
    }
}

// Wrappers preserve undefined separately from null without reserving any user data value.
fn defined(value) => if (value is error) value else {defined: true, value: value}
fn absent() => {defined: false, value: null}
fn truth(value) => value.defined and value.value != null and value.value != false and
    not (value.value is nan) and (not (value.value is number) or value.value != 0) and
    (not (value.value is string) or value.value != "")
fn numeric(value) {
    let raw = value.value;
    if (not value.defined) nan else if (raw == null or raw == false) 0.0
    else if (raw == true) 1.0 else if (raw is number) float(raw)
    else if (raw is string or raw is array) {
        let text = textual(value);
        if (trim(text) == "") 0.0 else float(text) ^ {nan}
    } else if (raw is datetime) float(raw.unix) else nan
}
fn textual(value) => if (not value.defined) "undefined" else if (value.value == null) "null"
    else if (value.value == true) "true" else if (value.value == false) "false"
    else if (value.value is nan) "NaN" else if (value.value == inf) "Infinity" else if (value.value == -inf) "-Infinity"
    else if (value.value is array) join([for (item in value.value) if (item == null) "" else textual(defined(item))], ",")
    else if (value.value is map or value.value is element) "[object Object]" else string(value.value)
fn strict_equal(a, b) => if (not a.defined or not b.defined) a.defined == b.defined
    else if (a.value is number and b.value is number) a.value == b.value
    else type(a.value) == type(b.value) and a.value == b.value
fn loose_equal(a, b) => if ((not a.defined or a.value == null) and (not b.defined or b.value == null)) true
    else if (type(a.value) == type(b.value)) strict_equal(a, b)
    else if ((a.value is number or a.value is bool or a.value is string) and
             (b.value is number or b.value is bool or b.value is string)) numeric(a) == numeric(b) else false
fn property(object_value, key) {
    let value = object_value.value;
    let name = key.value;
    if (not object_value.defined or value == null) error("chart: expression property access on null or undefined")
    else if ((value is array or value is string) and name == "length") defined(len(value))
    else if (value is array or value is string) {
        let index = int(name) ^ {null};
        if (index == null or index < 0 or index >= len(value) or
            (if (name is number) name != index else string(index) != name)) absent()
        else defined(if (value is string) slice(value, index, index + 1) else value[index])
    } else if (value is map or value is element) {
        let fields = if (value is element) map(value) else value;
        if (contains([for (key, item in fields) string(key)], string(name))) defined(value[string(name)]) else absent()
    } else absent()
}
fn int32(value) {
    let n = numeric(value);
    if (not util.finite_number(n) or n == 0) 0 else {
        let unsigned = ((if (n < 0) ceil(n) else floor(n)) % 4294967296.0 + 4294967296.0) % 4294967296.0;
        int(if (unsigned >= 2147483648.0) unsigned - 4294967296.0 else unsigned)
    }
}
fn binary_value(op, a, b) {
    let x = numeric(a);
    let y = numeric(b);
    if (op == "==" or op == "!=") (if (op == "==") loose_equal(a, b) else not loose_equal(a, b))
    else if (op == "===" or op == "!==") (if (op == "===") strict_equal(a, b) else not strict_equal(a, b))
    else if (contains(["<", "<=", ">", ">="], op)) {
        let left = if (a.value is string and b.value is string) a.value else x;
        let right = if (a.value is string and b.value is string) b.value else y;
        if (left is nan or right is nan) false else if (op == "<") left < right
        else if (op == "<=") left <= right else if (op == ">") left > right else left >= right
    } else if (op == "+") (if (a.value is string or b.value is string) textual(a) ++ textual(b) else x + y)
    else if (op == "-") x - y else if (op == "*") x * y else if (op == "/") x / y
    else if (op == "%") x % y else if (op == "**") x ** y
    else if (op == "&") band(int32(a), int32(b)) else if (op == "|") bor(int32(a), int32(b))
    else if (op == "^") bxor(int32(a), int32(b))
    else if (op == "<<") int32(defined(float(int32(a)) * (2.0 ** float(band(int32(b), 31)))))
    else if (op == ">>") shr(int32(a), band(int32(b), 31))
    else if (op == ">>>") floor((if (int32(a) < 0) int32(a) + 4294967296.0 else int32(a)) / (2.0 ** float(band(int32(b), 31))))
    else error("chart: expression unsupported operator " ++ string(op))
}
fn evaluate_list(nodes, row) {
    let values = [for (node in nodes) run(node, row)];
    let failure = util.first_error(values);
    if (failure is error) failure else values
}
fn call_function(function_name, args) {
    let values = args |> ~.value;
    let a = values[0];
    let b = values[1];
    let c = values[2];
    let x = numeric(args[0]);
    if (function_name == "isArray") a is array else if (function_name == "isBoolean") a is bool
    else if (function_name == "isDate") a is datetime else if (function_name == "isDefined") args[0].defined
    else if (function_name == "isNumber") a is number else if (function_name == "isObject") a != null and (a is map or a is element or a is array or a is datetime)
    else if (function_name == "isString") a is string else if (function_name == "isValid") args[0].defined and a != null and not (a is nan)
    else if (function_name == "isNaN") a is nan else if (function_name == "isFinite") util.finite_number(a)
    else if (function_name == "toBoolean") (if (a == null or a == "") null else truth(args[0]))
    else if (function_name == "toNumber") (if (a == null or a == "") null else x)
    else if (function_name == "toString") (if (a == null or a == "") null else textual(args[0]))
    else if (numeric_functions[function_name] != null) numeric_functions[function_name](x)
    else if (value_functions[function_name] != null) value_functions[function_name](a)
    else if (function_name == "atan2") math.atan2(x, numeric(args[1]))
    else if (function_name == "clamp") max([numeric(args[1]), min([numeric(args[2]), x])])
    else if (function_name == "hypot") sqrt(sum([for (arg in args) numeric(arg) ** 2.0]))
    else if (function_name == "max" or function_name == "min") {
        let numbers = args |> numeric(~);
        if (len(numbers |: ~ is nan) > 0) nan else if (len(numbers) == 0) (if (function_name == "max") -inf else inf)
        else if (function_name == "max") max(numbers) else min(numbers)
    } else if (function_name == "pow") x ** numeric(args[1])
    else if (function_name == "indexof" or function_name == "lastindexof") {
        let hits = if (a is array) [for (i, item in a where strict_equal(defined(item), args[1])) i]
            else [for (i in 0 to (len(a) - len(b)) where slice(a, i, i + len(b)) == b) i];
        if (len(hits) == 0) -1 else hits[if (function_name == "indexof") 0 else len(hits) - 1]
    } else if (function_name == "slice" or function_name == "substring") {
        let start = int(numeric(args[1]));
        let end = if (len(args) == 3) int(numeric(args[2])) else len(a);
        if (function_name == "substring") slice(a, max([0, min([start, end])]), max([0, max([start, end])]))
        else slice(a, if (start < 0) max([0, len(a) + start]) else start, if (end < 0) max([0, len(a) + end]) else end)
    } else if (function_name == "split") take(split(a, b), if (len(args) == 3) max([0, int(c)]) else len(a) + 1)
    else if (function_name == "join") join([for (item in a) if (item == null) "" else textual(defined(item))], if (len(args) == 2) b else ",")
    else if (function_name == "replace") {
        let index = index_of(a, b);
        if (index == null) a else slice(a, 0, index) ++ c ++ slice(a, index + len(b))
    } else if (function_name == "inrange") a >= min([b[0], b[len(b) - 1]]) and a <= max([b[0], b[len(b) - 1]])
    else if (function_name == "extent") (let valid = a |: ~ != null and not (~ is nan), [min(valid), max(valid)])
    else if (function_name == "peek") a[len(a) - 1] else if (function_name == "reverse") reverse(a)
    else if (function_name == "sort") sort(a) else if (function_name == "span") a[len(a) - 1] - a[0]
    else if (function_name == "lerp") a[0] + (a[len(a) - 1] - a[0]) * b
    else if (function_name == "pluck") [for (item in a) nested_property(item, split(b, "."), 0)]
    else if (function_name == "merge") merge_objects(values, 0, {})
    else if (function_name == "format") numbers.format_number(a, b)
    else error("chart: expression unknown function " ++ string(function_name))
}
fn nested_property(value, keys, index) => if (index >= len(keys)) value else nested_property(value[keys[index]], keys, index + 1)
fn merge_objects(values, index, result) => if (index >= len(values)) result
    else merge_objects(values, index + 1, {*:result, *:values[index]})
fn run(node, row) {
    if (node.kind == "literal") defined(node.value)
    else if (node.kind == "undefined") absent()
    else if (node.kind == "datum") defined(row)
    else if (node.kind == "array" or node.kind == "object_value") {
        let values = evaluate_list(if (node.kind == "array") node.items else node.items |> ~.node, row);
        if (values is error) values else defined(if (node.kind == "array") values |> ~.value
            else map([for (i, item in node.items) for (pair in [item.key, values[i].value]) pair]))
    } else if (node.kind == "member") {
        let object_value = run(node.object_value, row);
        let key = run(node.key, row);
        if (object_value is error) object_value else if (key is error) key else property(object_value, key)
    } else if (node.kind == "unary") {
        let value = run(node.arg, row);
        if (value is error) value else defined(if (node.op == "!") not truth(value)
            else if (node.op == "~") bnot(int32(value)) else if (node.op == "-") -numeric(value) else numeric(value))
    } else if (node.kind == "conditional") {
        let test = run(node.test, row);
        if (test is error) test else run(if (truth(test)) node.yes else node.no, row)
    } else if (node.kind == "binary") {
        let left = run(node.left, row);
        if (left is error) left else if (node.op == "&&" and not truth(left)) left
        else if (node.op == "||" and truth(left)) left else {
            let right = run(node.right, row);
            if (right is error) right else if (node.op == "&&" or node.op == "||") right
            else defined(binary_value(node.op, left, right))
        }
    } else if (node.kind == "call" and node.function_name == "if") {
        let test = run(node.args[0], row);
        if (test is error) test else run(node.args[if (truth(test)) 1 else 2], row)
    } else if (node.kind == "call") {
        let args = evaluate_list(node.args, row);
        if (args is error) args else defined(call_function(node.function_name, args))
    } else error("chart: invalid compiled expression")
}

pub fn evaluate(compiled, row) {
    if (compiled is error) compiled else {
        let result = run(compiled, row);
        if (result is error) result else result.value
    }
}
pub fn test(compiled, row) {
    if (compiled is error) compiled else {
        let result = run(compiled, row);
        if (result is error) result else truth(result)
    }
}
