// @expect-error: E103
// @description: `T[n, m]` is retired (S11.1.6v2, S16.8.6v3); the spelling is `T{n,m}`, or `[T{n,m}]` for a counted array.
// Registered in test_lambda_errors_gtest (P5 of vibe/impl/Lambda_List_Fixes (done).md).

type Coordinates = float[2, 3]
