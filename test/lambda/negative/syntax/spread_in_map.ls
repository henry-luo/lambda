// @expect-error: E100
// @description: '...' spread syntax is not valid in Lambda — use '*expr' or implicit merge

{...a, b: 123}
