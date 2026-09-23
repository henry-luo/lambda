// @expect-error: E103
// @description: the open count is `T{n+}` (S11.1.6v2, S16.8.6v3); regex's
// trailing comma `T{n,}` is not the spelling, and an exact `T{n}` would be a
// silently different type.
// Registered in test_lambda_errors_gtest (P5 of vibe/impl/Lambda_List_Fixes (done).md).

type AtLeastTwo = int{2,}
