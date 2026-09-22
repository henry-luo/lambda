// @expect-error: E100
// @description: `T[n+]` is retired (S11.1.6, S16.8.6v2); the spelling is `T{n,}`.
// Registered in test_lambda_errors_gtest by phase P5 of vibe/impl/Lambda_List_Fixes.md.

type Polygon = int[3+]
