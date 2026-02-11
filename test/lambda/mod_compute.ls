// module with computed pub let variables (expressions, not just literals)
pub doubled = 21 * 2
pub label = "version-" ++ string(3)
pub total = len([1, 2, 3, 4, 5])
pub nums = [for (i in 1 to 5) i * 10]

pub fn describe() { label ++ ":" ++ string(total) }
