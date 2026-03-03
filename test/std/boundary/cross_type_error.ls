// Test: Cross-Type Error
// Layer: 3 | Category: boundary | Covers: error is, truthiness, or default, in collection

// ===== Error is check =====
let e = error("test")
e is error
e is string
e is int

// ===== Error truthiness (falsy) =====
if (e) "truthy" else "falsy"

// ===== Error or default =====
e or "default value"
e or 42
e or [1, 2, 3]

// ===== Error in collection =====
let items = [1, error("err"), "hello", null]
len(items)
items[0]
items[1] is error
items[2]
items[3]

// ===== Error vs null =====
let e2 = error("test")
e2 == null
e2 != null
null is error

// ===== Error message access =====
e.message
e.code

// ===== Error with code =====
let coded = error("bad", code: "E001")
coded.message
coded.code
coded is error

// ===== Error comparison =====
let e3 = error("test")
let e4 = error("test")
let e5 = error("other")
// errors are compared by reference or value
e3 is error
e5 is error

// ===== Error in or chain =====
error("a") or error("b") or "final"

// ===== Error in conditional =====
let result = if (error("x")) "truthy" else "falsy"
result

// ===== Error and short-circuit =====
error("x") and "never reached"
true and error("x")

// ===== Error in map value =====
let m = {ok: 42, fail: error("nope")}
m.ok
m.fail is error

// ===== Filter out errors =====
[1, error("a"), 2, error("b"), 3] | filter((x) => x is int)
