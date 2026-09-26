// S11.1.7: `none` admits no value, so a statically known value never crosses
// into it -- a declaration and a declared return are both compile errors.
let wrong_none: none = 5;
fn none_return() none => "x";
[wrong_none, none_return()]
