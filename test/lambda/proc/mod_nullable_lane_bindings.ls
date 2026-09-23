// Helper module for nullable_lane_bindings.ls: `bool?` and `string?` results
// that cross an import call before a binding stores them.
pub fn remote_flag(x: int) bool? { if (x > 0) true else null }
pub fn remote_text(x: int) string? { if (x > 0) "far" else null }
