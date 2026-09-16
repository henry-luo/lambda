// T12-8: ordinary capture-free global regexes avoid per-match exec results;
// overridden exec, captures, callbacks and descriptor changes retain protocol.
var words_rx = new RegExp("[a-z]+", "g");
var words = "one 22 two".match(words_rx);
var replace_rx = new RegExp("[a-z]+", "g");
var replaced = "one 22 two".replace(replace_rx, "[$&]");
var empty_rx = new RegExp("x*", "g");
var empty = "ab".match(empty_rx);
var empty_replace_rx = new RegExp("x*", "g");
var empty_replaced = "ab".replace(empty_replace_rx, "-");

var custom = new RegExp("a", "g");
var custom_calls = 0;
function custom_exec(value) {
    custom_calls = custom_calls + 1;
    return null;
}
custom.exec = custom_exec;
var custom_result = "a".match(custom);

var symbol_match_rx = new RegExp("a", "g");
function custom_symbol_match(value) { return "symbol-match:" + value; }
symbol_match_rx[Symbol.match] = custom_symbol_match;
var symbol_match_result = "a".match(symbol_match_rx);

var symbol_replace_rx = new RegExp("a", "g");
function custom_symbol_replace(value, replacement) {
    return "symbol-replace:" + value + ":" + replacement;
}
symbol_replace_rx[Symbol.replace] = custom_symbol_replace;
var symbol_replace_result = "a".replace(symbol_replace_rx, "x");

var captured_rx = new RegExp("(a)", "g");
var captured = "aba".replace(captured_rx, "<$1>");
var callback_calls = 0;
var callback_rx = new RegExp("a", "g");
function upper_callback(value) {
    callback_calls = callback_calls + 1;
    return value.toUpperCase();
}
var callback_result = "aba".replace(callback_rx, upper_callback);

var frozen_index = new RegExp("a", "g");
Object.defineProperty(frozen_index, "lastIndex", { writable: false });
var index_error = "none";
try {
    "a".match(frozen_index);
} catch (error) {
    index_error = error.name;
}

console.log("match:" + words.join(","));
console.log("replace:" + replaced);
console.log("empty:" + empty.length + ":" + empty.join("|"));
console.log("empty-replace:" + empty_replaced);
console.log("custom:" + custom_result + ":" + custom_calls);
console.log("symbol:" + symbol_match_result + "," + symbol_replace_result);
console.log("capture:" + captured);
console.log("callback:" + callback_result + ":" + callback_calls);
console.log("last-index:" + index_error);
