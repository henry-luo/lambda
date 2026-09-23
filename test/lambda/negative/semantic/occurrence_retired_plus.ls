// @expect-error: E103
// @description: `T[n+]` is retired (S11.1.6v2, S16.8.6v3); the spelling is `T{n+}`.
// Registered in test_lambda_errors_gtest (P5 of vibe/impl/Lambda_List_Fixes (done).md).

type Polygon = int[3+]
