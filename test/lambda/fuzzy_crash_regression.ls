// Regression tests for crashes discovered by fuzzy testing (2026-04-16)
//
// Covers:
// - Crash 1: argmin/argmax on empty array (segfault in vector_get on NULL items)
// - Crash 2: for-in comprehension on error value (iter_len returned INT64_MAX)
// - Crash 3: ~ self-reference in type method body (in_pipe not set in MIR transpiler)
// - Crash 4: len() on null from query (tagged null Item passed as raw pointer)

// === Crash 1: argmin/argmax on empty array ===
'Crash 1: argmin/argmax empty array'
argmin([])
argmax([])

// === Crash 2: for-in on error/undefined value ===
'Crash 2: for-in on error value'
[for (c in undefined_var) c]

// === Crash 3: ~ self-reference in type method body ===
'Crash 3: ~ in type method'
type Vec {
    x: float,
    y: float;
    fn translate(dx: float, dy: float) => <Vec *:~, x: x + dx, y: y + dy>
    fn scale(factor: float) => <Vec *:~, x: x * factor, y: y * factor>
}
let v = <Vec x: 3.0, y: 4.0>
v.translate(1.0, -1.0) != null
v.scale(2.0) != null

// === Crash 4: len() on null from optional query ===
'Crash 4: len on null from query'
let items = <root>
    <item> "a"
    <item> "b"
len(items?item)
len(items?nonexistent)
