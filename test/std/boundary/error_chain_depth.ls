// Test: Error Chain Depth
// Layer: 3 | Category: boundary | Covers: deep error chaining, source traversal

// ===== Single error =====
let e1 = error("level 1")
e1.message

// ===== Error wrapping error =====
let e2 = error("level 2", source: e1)
e2.message
e2.source.message

// ===== Three levels deep =====
let e3 = error("level 3", source: e2)
e3.message
e3.source.message
e3.source.source.message

// ===== Four levels deep =====
let e4 = error("level 4", source: e3)
e4.message
e4.source.source.source.message

// ===== Five levels deep =====
let e5 = error("level 5", source: e4)
e5.message
e5.source.source.source.source.message

// ===== Error with code at each level =====
let coded1 = error("DB connection failed", code: "DB_ERR")
let coded2 = error("Query failed", code: "QUERY_ERR", source: coded1)
let coded3 = error("Request failed", code: "REQ_ERR", source: coded2)
coded3.code
coded3.source.code
coded3.source.source.code

// ===== Error chain in function =====
fn chain_errors(depth: int) {
    if (depth <= 0)
        error("base error")
    else
        error("error at " & str(depth), source: chain_errors(depth - 1))
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
