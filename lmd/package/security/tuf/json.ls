// D11.2.4: strict JSON and OLPC signing bytes. Keep integer lexemes exact:
// metadata versions and unknown signed fields need not fit a machine integer.
import utf8: lambda.io.utf8

fn invalid(message) error => error("tuf-json: " ++ message)
// S11.4.2v3: typed reads/slices can fail admission and must reach read's caller.
fn ch(source: string[], at: int) string^ => if (at < 0 or at >= len(source)) "" else source[at]
fn span(source: string[], first: int, end: int) string^ => join(slice(source, first, end)^, "")
// string contracts keep generic text operations on their non-error branch.
fn digit(value: string) bool => value != "" and contains("0123456789", value)
// Flat token length must not consume call depth: metadata can contain long
// public keys, signatures and unknown fields within the host's byte ceiling.
fn whitespace(source: string[], at: int) int^ {
    var current = at
    while (current < len(source) and contains([" ", "\t", "\r", "\n"], ch(source, current)^)) {
        current = current + 1
    }
    current
}
fn digits_end(source: string[], at: int) int^ {
    var current = at
    while (digit(ch(source, current)^)) { current = current + 1 }
    current
}
fn hex4(source: string[], at: int) int^ {
    let text = lower(span(source, at, at + 4)^)
    let values = [for (c in split(text, "")) index_of("0123456789abcdef", c)];
    if (len(text) != 4 or any([for (v in values) v == null])) raise invalid("invalid Unicode escape")
    else values[0] * 4096 + values[1] * 256 + values[2] * 16 + values[3]
}
fn string_end(source: string[], at: int) int^ {
    var current = at
    while (current < len(source)) {
        let c = ch(source, current)^;
        if (c == "\"") { return current + 1 }
        if (ord(c) < 32) raise invalid("unescaped control character")
        if (c != "\\") { current = current + 1 }
        else {
            let escape = ch(source, current + 1)^
            let simple = contains(["\"", "\\", "/", "b", "f", "n", "r", "t"], escape);
            if (not simple and escape != "u") raise invalid("invalid string escape")
            if (simple) { current = current + 2 }
            else {
                let cp = hex4(source, current + 2)^;
                if (cp >= 56320 and cp <= 57343) raise invalid("unpaired low surrogate")
                if (cp < 55296 or cp > 56319) { current = current + 6 }
                else {
                    let low = if (span(source, current + 6, current + 8)^ == "\\u") hex4(source, current + 8)^ else -1;
                    if (low < 56320 or low > 57343) raise invalid("unpaired high surrogate")
                    current = current + 12
                }
            }
        }
    }
    raise invalid("unterminated string")
}
fn string_node(source: string[], at: int) map^ {
    let end = string_end(source, at + 1)^
    let text = parse(span(source, at, end)^, 'json')^;
    {node: {kind: 'string', text: text}, next: end}
}
fn number_node(source: string[], at: int) map^ {
    let digit_start = if (ch(source, at)^ == "-") at + 1 else at
    let first = ch(source, digit_start)^
    let end = if (first == "0") digit_start + 1 else digits_end(source, digit_start)^
    let text = span(source, at, end)^;
    if (not digit(first) or (first == "0" and digit(ch(source, end)^))) raise invalid("invalid integer")
    else if (contains([".", "e", "E"], ch(source, end)^)) raise invalid("floating point signing data is unsupported")
    else {node: {kind: 'number', text: if (text == "-0") "0" else text}, next: end}
}
fn array_items(source: string[], at: int, depth: int) map^ {
    var current = whitespace(source, at)^, collected = []
    if (ch(source, current)^ == "]") { return {node: {kind: 'array', items: collected}, next: current + 1} }
    while (true) {
        let item = value(source, current, depth + 1)^
        let next = whitespace(source, item.next)^
        collected = [*collected, item.node]
        if (ch(source, next)^ == "]") { return {node: {kind: 'array', items: collected}, next: next + 1} }
        if (ch(source, next)^ != "," or ch(source, whitespace(source, next + 1)^)^ == "]")
            raise invalid("invalid array separator")
        current = whitespace(source, next + 1)^
    }
    raise invalid("unterminated array")
}
fn object_items(source: string[], at: int, depth: int) map^ {
    var current = whitespace(source, at)^, entries = []
    if (ch(source, current)^ == "}") { return {node: {kind: 'object', entries: entries}, next: current + 1} }
    while (true) {
        if (ch(source, current)^ != "\"") raise invalid("object key must be a string")
        let key = string_node(source, current)^
        let colon = whitespace(source, key.next)^
        if (ch(source, colon)^ != ":") raise invalid("missing object colon")
        if (any([for (entry in entries) entry.name == key.node.text])) raise invalid("duplicate object key")
        let item = value(source, whitespace(source, colon + 1)^, depth + 1)^
        let next = whitespace(source, item.next)^
        entries = [*entries, {name: key.node.text, node: item.node}]
        if (ch(source, next)^ == "}") { return {node: {kind: 'object', entries: entries}, next: next + 1} }
        if (ch(source, next)^ != "," or ch(source, whitespace(source, next + 1)^)^ == "}")
            raise invalid("invalid object separator")
        current = whitespace(source, next + 1)^
    }
    raise invalid("unterminated object")
}
fn value(source: string[], at: int, depth: int) map^ {
    let c = ch(source, at)^;
    if (depth > 64) raise invalid("nesting limit")
    else if (c == "\"") string_node(source, at)^
    else if (c == "{") object_items(source, at + 1, depth)^
    else if (c == "[") array_items(source, at + 1, depth)^
    else if (span(source, at, at + 4)^ == "null") {node: {kind: 'null'}, next: at + 4}
    else if (span(source, at, at + 4)^ == "true") {node: {kind: 'bool', value: true}, next: at + 4}
    else if (span(source, at, at + 5)^ == "false") {node: {kind: 'bool', value: false}, next: at + 5}
    else if (c == "-" or digit(c)) number_node(source, at)^
    else raise invalid("expected JSON value")
}

pub fn read(bytes) map^ {
    if (not (bytes is string or bytes is binary) or len(binary(bytes)) > 1048576) raise invalid("input limit or type")
    else {
        let source = split(utf8.decode(binary(bytes)^)^, "")
        let parsed = value(source, whitespace(source, 0)^, 0)^;
        if (whitespace(source, parsed.next)^ != len(source)) raise invalid("trailing JSON content") else parsed.node
    }
}
pub fn get(node, member_name) => if (node == null or node.kind != 'object') null
    else [for (entry in node.entries where entry.name == member_name) entry.node][0]
pub fn text(node) string^ => if (node != null and node.kind == 'string') node.text else raise invalid("expected string")
pub fn members(node) array^ => if (node != null and node.kind == 'object') node.entries else raise invalid("expected object")
pub fn items(node) array^ => if (node != null and node.kind == 'array') node.items else raise invalid("expected array")
pub fn boolean(node) bool^ => if (node != null and node.kind == 'bool') node.value else raise invalid("expected boolean")
pub fn natural(node) string^ => if (node != null and node.kind == 'number' and not starts_with(node.text, "-")) node.text
    else raise invalid("expected nonnegative integer")
pub fn positive(node) string^ {
    let numeral = natural(node)^;
    if (numeral == "0") raise invalid("expected positive integer") else numeral
}
// Decimal comparisons and increment avoid a machine-integer ceiling for versions.
pub fn compare(left, right) => if (len(left) < len(right)) -1 else if (len(left) > len(right)) 1
    else if (left < right) -1 else if (left > right) 1 else 0
pub fn increment(numeral: string) string^ {
    let pivots = [for (i in 0 to len(numeral) - 1 where slice(numeral, i, i + 1)^ != "9") i]
    let pivot = if (len(pivots) == 0) -1 else pivots[len(pivots) - 1]
    let zeroes = join([for (i in 0 to len(numeral) - pivot - 2) "0"], "");
    if (pivot < 0) "1" ++ zeroes
    else slice(numeral, 0, pivot) ++ string(index_of("0123456789", slice(numeral, pivot, pivot + 1)) + 1) ++ zeroes
}
fn signing_quoted(text: string) string => "\"" ++ replace(replace(text, "\\", "\\\\"), "\"", "\\\"") ++ "\""
fn quoted(text, document) string^ => if (document) format(text, 'json')^ else signing_quoted(text)
fn serialize(node, depth, document) string^ {
    if (node == null or depth > 64) raise invalid("invalid canonical node or depth")
    else if (node.kind == 'null') "null"
    else if (node.kind == 'bool') if (node.value) "true" else "false"
    else if (node.kind == 'number') node.text
    else if (node.kind == 'string') quoted(node.text, document)^
    else if (node.kind == 'array') "[" ++ join([for (child in node.items) serialize(child, depth + 1, document)^], ",") ++ "]"
    else if (node.kind == 'object') {
        let entries = sort(node.entries, {by: (entry) => entry.name});
        "{" ++ join([for (entry in entries) quoted(entry.name, document)^ ++ ":" ++ serialize(entry.node, depth + 1, document)^], ",") ++ "}"
    } else raise invalid("invalid canonical node")
}
pub fn canonical(node, depth = 0) string^ => serialize(node, depth, false)^
pub fn document(node) string^ => serialize(node, 0, true)^
pub fn canonical_text(bytes) string^ => canonical(read(bytes)^)^
