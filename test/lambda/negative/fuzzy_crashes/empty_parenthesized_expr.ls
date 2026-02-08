// Test: Empty or comment-only parenthesized expressions should not crash
// This previously caused a segfault in build_primary_expr when build_expr
// returned NULL for a comment inside parentheses, and the result was
// dereferenced without a null check.

// Parenthesized comment-only expression (was the original crash)
(// comment
)

// Nested parenthesized comment-only expression
((// another comment
))

// Deeply nested parentheses around a comment
(((// deep comment
)))
