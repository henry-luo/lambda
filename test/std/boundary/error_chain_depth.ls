// Test: Error Chain Depth
// Layer: 3 | Category: boundary | Covers: deep error chaining, source traversal
// S7.4.4: error(msg, source) wraps an inner error that `.source` reads back.

// ===== Single error =====
let e1 = error("level 1")
e1.message

// ===== Error wrapping error =====
let e2 = error("level 2", e1)
e2.message
e2.source.message

// ===== Three levels deep =====
let e3 = error("level 3", e2)
e3.message
e3.source.message
e3.source.source.message

// ===== Four levels deep =====
let e4 = error("level 4", e3)
e4.message
e4.source.source.source.message

// ===== Five levels deep =====
let e5 = error("level 5", e4)
e5.message
e5.source.source.source.source.message
e5.source.source.source.source.source

// ===== Error with code at each level =====
let coded1 = error({code: 501, message: "DB connection failed"})
let coded2 = error({code: 502, message: "Query failed", source: coded1})
let coded3 = error({code: 503, message: "Request failed", source: coded2})
coded3.code
coded3.source.code
coded3.source.source.code

// ===== Error chain in function =====
fn chain_errors(depth: int) error {
    if (depth <= 0)
        error("base error")
    else
        error("error at " ++ string(depth), chain_errors(depth - 1))
}
let chained = chain_errors(3)
chained.message
chained.source.message
chained.source.source.message
chained.source.source.source.message

// ===== Error chain is falsy =====
if (e5) "truthy" else "falsy"

// ===== Or default on deep chain =====
e5 or "default"
