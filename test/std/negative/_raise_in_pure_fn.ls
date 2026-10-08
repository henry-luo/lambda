// PARKED 2026-10-07 — LR10-19: S7.4.2 licenses `raise` only in `T^` functions; this script is
// rejected, but by E208 about the `error()` call rather than by the raise rule.
// Test discovery skips `_` scripts. When fixed, move it to test/lambda/negative/semantic/
// and assert its diagnostic in test/test_lambda_errors_gtest.cpp (ExpectErrorMessage).
// Test: Raise in Pure Function
// Layer: 2 | Category: negative | Covers: use raise in fn with plain T return

// Function with plain return type using raise - should be compile error
fn bad_function() int {
    raise error("not allowed")
}

bad_function()
