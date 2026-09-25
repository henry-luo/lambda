// @expect-error: E100
// @description: S10.3.1v3: infix `where` is the retired filter spelling; the
// diagnostic names the filter stage `|:`. `where` survives only as a `for`
// header clause.

[1, 2, 3] where ~ > 1
