// an Error is a successful argument to an explicit Error contract (S11.4.2v3).
import outcome: ._mir_mvp_error_boundary_module
fn dynamic(value: any) any => value
fn describe(problem: error) string => problem.message
fn relay(operation, value: any) any => operation(value)
// S7.6.3v3/S11.4.2v3: known Errors use the admitted error arm of a scalar outcome.
fn returned_error(problem: error) int^ => problem
fn propagated_error(problem: any) int^ => problem^
let problem = error({code: 7005, message: "kept"});
[describe(problem), describe(dynamic(problem)), relay(describe, problem),
 describe(outcome.failure("imported")), relay(describe, outcome.failure("dynamic")),
 returned_error(problem) ^ { ^.message }, propagated_error(problem) ^ { ^.message }]
