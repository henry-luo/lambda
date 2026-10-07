// PARKED 2026-10-07 — LR02-35: an unknown variable name compiles and evaluates to a bare
// `error` instead of the compile error this script expects; no S# ruling covers unknown names yet.
// Test discovery skips `_` scripts. When fixed, move it to test/lambda/negative/semantic/
// and assert its diagnostic in test/test_lambda_errors_gtest.cpp (ExpectErrorMessage).
// Test: Undefined Variable
// Layer: 2 | Category: negative | Covers: reference undefined name

// Reference undefined variable - should produce error E202
undefined_var

// Use undefined in expression
1 + unknown_name

// Undefined in function body
fn test() => nonexistent_var
test()
