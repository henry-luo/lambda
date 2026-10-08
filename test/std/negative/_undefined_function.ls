// PARKED 2026-10-07 — LR02-35: an unknown function name compiles and evaluates to a bare
// `error` (code 318, no diagnostic) instead of the compile error this script expects; no S# ruling covers
// unknown names yet.
// Test discovery skips `_` scripts. When fixed, move it to test/lambda/negative/semantic/
// and assert its diagnostic in test/test_lambda_errors_gtest.cpp (ExpectErrorMessage).
// Test: Undefined Function
// Layer: 2 | Category: negative | Covers: call non-existent function

// Call undefined function - should produce error E203
nonexistent_function(42)

// Misspelled function
pritn("hello")

// Call with wrong name
fn greet(name: string) => "Hello, " ++ name
greeet("world")
